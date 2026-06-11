#include "receiver_ui_plugin.h"
#include "logos_api.h"
#include "logos_sdk.h"
#include "logos_types.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {
constexpr int    kTtlMs       = 45000;   // drop a station after 45s without a heartbeat (3 missed beats)
constexpr int    kPruneMs     = 5000;
const char* const kSettingsOrg = "logos";
const char* const kSettingsApp = "receiver_ui";
const char* const kDirTopic    = "/radio-basecamp/1/directory/json";
}

ReceiverUiPlugin::ReceiverUiPlugin(QObject* parent)
    : ReceiverUiSimpleSource(parent)
{
}

ReceiverUiPlugin::~ReceiverUiPlugin()
{
    stopPlayback();
    delete m_logos;
}

void ReceiverUiPlugin::initLogos(LogosAPI* api)
{
    if (m_logos) return;
    m_logosAPI = api;
    m_logos = new LogosModules(api);   // SAFE in ui-host (the exact call that crashes in a core sidecar)
    setBackend(this);

    // restore persisted settings (these are the generated READONLY-PROP source-side setters)
    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    setListenBuffer(qBound(2, s.value(QStringLiteral("listenBuffer"), 8).toInt(), 20));
    setHideCache(s.value(QStringLiteral("hideCache"), false).toBool());

    wireEvents();

    m_pruneTimer = new QTimer(this);
    m_pruneTimer->setInterval(kPruneMs);
    QObject::connect(m_pruneTimer, &QTimer::timeout, this, &ReceiverUiPlugin::pruneStations);
    m_pruneTimer->start();

    // Auto-start discovery once the event loop + QRO registry have settled
    // (skill: ipc-client-eager-init — avoid touching the client straight out of initLogos).
    QTimer::singleShot(2500, this, [this]{ startDiscovery(); });
}

void ReceiverUiPlugin::wireEvents()
{
    m_logos->delivery_module.on("connectionStateChanged", [this](const QVariantList& d) {
        if (!d.isEmpty()) setConnectionStatus(d.at(0).toString());
    });
    m_logos->delivery_module.on("messageReceived", [this](const QVariantList& d) {
        // d: [0]=hash, [1]=topic, [2]=payload, [3]=timestamp(ns). The announce is in [2].
        if (d.size() < 3) return;
        ingestAnnounce(d.at(2));
    });
}

QString ReceiverUiPlugin::startDiscovery()
{
    if (!m_logos) return QStringLiteral("backend not initialised");

    if (!nodeReady()) {
        // preset logos.dev + relay:true to interop with live radio-basecamp hosts (e.g. Sneg's
        // "Logos manifesto"); the demo defaults logos.test — we must match the host's fleet.
        QJsonObject cfg{
            {"logLevel", "INFO"},
            {"mode", "Core"},
            {"preset", "logos.dev"},
            {"relay", true}
        };
        const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));

        LogosResult c = m_logos->delivery_module.createNode(cfgJson);
        if (!c.success) { setLastError(c.getError()); log("createNode failed: " + c.getError()); return c.getError(); }

        LogosResult st = m_logos->delivery_module.start();
        if (!st.success) { setLastError(st.getError()); log("start failed: " + st.getError()); return st.getError(); }

        setNodeReady(true);
        log("delivery node started (logos.dev)");
    }

    if (!subscribeTopic(directoryTopic()))
        return QStringLiteral("subscribe failed");

    setDiscovering(true);
    log("discovering on " + directoryTopic());
    return QString();
}

QString ReceiverUiPlugin::stopDiscovery()
{
    if (!m_logos) return QStringLiteral("backend not initialised");
    for (const QString& t : m_subscribed)
        m_logos->delivery_module.unsubscribe(t);
    m_subscribed.clear();
    setDiscovering(false);
    log("discovery stopped");
    return QString();
}

QString ReceiverUiPlugin::addTopic(QString topic)
{
    topic = topic.trimmed();
    if (topic.isEmpty()) return QStringLiteral("empty topic");
    if (!subscribeTopic(topic)) return QStringLiteral("subscribe failed");
    log("added topic " + topic);
    return QString();
}

bool ReceiverUiPlugin::subscribeTopic(const QString& topic)
{
    if (!m_logos) return false;
    if (m_subscribed.contains(topic)) return true;
    LogosResult r = m_logos->delivery_module.subscribe(topic);
    if (!r.success) { setLastError(r.getError()); return false; }
    m_subscribed.insert(topic);
    return true;
}

void ReceiverUiPlugin::ingestAnnounce(const QVariant& payload)
{
    // Robust decode: radio hosts send the announce JSON; on receipt the payload is a base64 string
    // (radio's proven single decode). Fall back to raw UTF-8 / raw bytes so we parse regardless.
    QByteArray json;
    const QString asStr = payload.toString();
    if (!asStr.isEmpty()) {
        QByteArray b = QByteArray::fromBase64(asStr.toUtf8());
        if (!b.isEmpty() && b.trimmed().startsWith('{')) json = b;
        else if (asStr.trimmed().startsWith('{'))        json = asStr.toUtf8();
    }
    if (json.isEmpty()) {
        const QByteArray raw = payload.toByteArray();
        if (raw.trimmed().startsWith('{')) json = raw;
    }
    if (json.isEmpty()) return;

    const QJsonObject o = QJsonDocument::fromJson(json).object();
    const QString name = o.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) return;

    Station s;
    s.name      = name;
    s.host      = o.value(QStringLiteral("hostLabel")).toString();
    s.streamUrl = o.value(QStringLiteral("streamUrl")).toString();
    s.privacy   = o.value(QStringLiteral("privacy")).toString();
    s.topic     = o.value(QStringLiteral("announceTopic")).toString();
    s.lastSeenMs = QDateTime::currentMSecsSinceEpoch();

    const QString key = s.topic + "|" + s.name;
    const bool isNew = !m_stations.contains(key);
    m_stations.insert(key, s);
    if (isNew) log("discovered \"" + s.name + "\"");
    publishStations();
}

void ReceiverUiPlugin::pruneStations()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (auto it = m_stations.begin(); it != m_stations.end(); ) {
        if (now - it.value().lastSeenMs > kTtlMs) { it = m_stations.erase(it); changed = true; }
        else ++it;
    }
    if (changed) publishStations();
}

void ReceiverUiPlugin::publishStations()
{
    QJsonArray arr;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const Station& s : m_stations) {
        QJsonObject o;
        o["name"]      = s.name;
        o["host"]      = s.host;
        o["streamUrl"] = s.streamUrl;
        o["privacy"]   = s.privacy;
        o["topic"]     = s.topic;
        o["uptimeS"]   = (double)((now - s.lastSeenMs) / 1000);
        arr.append(o);
    }
    setStationsJson(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

QString ReceiverUiPlugin::play(QString streamUrl, QString stationName)
{
    // Security seam: only http(s) — reject file:/pipe:/concat: so an attacker-controlled announce
    // URL is safe to hand to ffplay (lifted from radio-basecamp).
    const QUrl u(streamUrl);
    const QString scheme = u.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return QStringLiteral("refused: only http(s) stream URLs are allowed");

    stopPlayback();

    const bool onion = u.host().endsWith(QLatin1String(".onion"));
    QStringList args;
    QString program;
    if (onion) {
        program = QStringLiteral("torsocks");
        args << QStringLiteral("ffplay");
    } else {
        program = QStringLiteral("ffplay");
    }
    args << QStringLiteral("-nodisp") << QStringLiteral("-autoexit")
         << QStringLiteral("-loglevel") << QStringLiteral("warning")
         // MediaMTX gates onion HLS with a Secure cookie ffmpeg won't return over http → supply it.
         << QStringLiteral("-cookies") << QStringLiteral("cookieCheck=1; path=/")
         // listener jitter buffer to ride out Tor latency (mpegts HLS)
         << QStringLiteral("-infbuf") << QStringLiteral("-live_start_index") << QString::number(-listenBuffer())
         << streamUrl;

    m_player = new QProcess(this);
    // Strip LD_LIBRARY_PATH/LD_PRELOAD so the spawned system tor/ffplay don't load the AppImage's
    // poisoned libs (skill: appimage-child-ld-library-path).
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("LD_LIBRARY_PATH"));
    env.remove(QStringLiteral("LD_PRELOAD"));
    m_player->setProcessEnvironment(env);
    m_player->start(program, args);
    if (!m_player->waitForStarted(3000)) {
        const QString err = m_player->errorString();
        delete m_player; m_player = nullptr;
        return QStringLiteral("playback failed to start: ") + err;
    }
    setNowPlaying(stationName.isEmpty() ? streamUrl : stationName);
    log("playing \"" + nowPlaying() + "\"");
    return QString();
}

QString ReceiverUiPlugin::stopPlayback()
{
    if (m_player) {
        m_player->kill();
        m_player->waitForFinished(1500);
        delete m_player;
        m_player = nullptr;
    }
    if (!nowPlaying().isEmpty()) { setNowPlaying(QString()); log("playback stopped"); }
    return QString();
}

QString ReceiverUiPlugin::setBuffer(int sec)
{
    sec = qBound(2, sec, 20);
    setListenBuffer(sec);   // generated PROP setter → auto-syncs to QML
    QSettings{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)}
        .setValue(QStringLiteral("listenBuffer"), sec);
    return QString();
}

QString ReceiverUiPlugin::setCacheHidden(bool on)
{
    setHideCache(on);       // generated PROP setter → auto-syncs to QML
    QSettings{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)}
        .setValue(QStringLiteral("hideCache"), on);
    if (on) clearCache();   // turning it on wipes whatever is already cached
    log(on ? "hide cache: ON (stream cache suppressed + cleared)" : "hide cache: OFF");
    return QString();
}

QString ReceiverUiPlugin::clearCache()
{
    const QString dir = cacheDir();
    if (dir.isEmpty()) return QString();
    QDir d(dir);
    if (d.exists()) d.removeRecursively();
    log("cache cleared");
    return QString();
}

QString ReceiverUiPlugin::cacheDir() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) return QString();
    return base + QStringLiteral("/receiver_ui");
}

QString ReceiverUiPlugin::directoryTopic() const
{
    return QString::fromLatin1(kDirTopic);
}

void ReceiverUiPlugin::log(const QString& line)
{
    qInfo() << "receiver_ui:" << line;
    emit activity(line);
}
