#include "receiver_ui_plugin.h"
#include "logos_api.h"
#include "logos_sdk.h"
#include "logos_types.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <dlfcn.h>

namespace {
constexpr int    kTtlMs       = 45000;   // drop a station after 45s without a heartbeat (3 missed beats)
constexpr int    kPruneMs     = 5000;
const char* const kSettingsOrg = "logos";
const char* const kSettingsApp = "receiver_ui";
const char* const kDirTopic    = "/radio-basecamp/1/directory/json";

bool isOnionUrl(const QString& url) { return QUrl(url).host().endsWith(QLatin1String(".onion")); }

QString randomHex(int bytes) {
    QString s;
    for (int i = 0; i < bytes; ++i) s += QString("%1").arg(QRandomGenerator::global()->bounded(256), 2, 16, QChar('0'));
    return s;
}

// Locate this .so on disk (so bundled helper binaries dropped next to it are found).
QString moduleDir() {
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(&isOnionUrl), &info) && info.dli_fname)
        return QFileInfo(QString::fromUtf8(info.dli_fname)).absolutePath();
    return QString();
}

// Resolve a runtime helper: env override → bundled (next to the .so, in bin/ or the module dir) →
// bare name on PATH. Lets the install drop tor/torsocks/ffplay into the module dir (self-contained,
// no system install) while still falling back to a system binary when present.
QString resolveBin(const QString& name, const char* envVar) {
    const QString env = qEnvironmentVariable(envVar);
    if (!env.isEmpty()) return env;
    const QString d = moduleDir();
    if (!d.isEmpty())
        for (const QString& c : { d + "/bin/" + name, d + "/" + name })
            if (QFileInfo(c).isExecutable()) return c;
    return name;  // fall back to PATH
}

// Spawned system binaries (tor/ffplay/torsocks) must NOT inherit the AppImage's LD_LIBRARY_PATH/
// LD_PRELOAD or they load the wrong libevent/etc and die (skill: appimage-child-ld-library-path).
QProcessEnvironment cleanSpawnEnv() {
    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    e.remove(QStringLiteral("LD_LIBRARY_PATH"));
    e.remove(QStringLiteral("LD_PRELOAD"));
    return e;
}
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
        // "Logos manifesto"). The deployed logos.dev preset ships NO bootstrap nodes (observed
        // bootstrapNodes=0 → currentPeerIds=[]), so supply the logos.dev entry nodes explicitly
        // (these are the built-in logos.dev bootstrap multiaddrs — the same peers Sneg relays through).
        QJsonArray entry{
            QStringLiteral("/dns4/delivery-01.do-ams3.logos.dev.status.im/tcp/30303/p2p/16Uiu2HAmTUbnxLGT9JvV6mu9oPyDjqHK4Phs1VDJNUgESgNSkuby"),
            QStringLiteral("/dns4/delivery-02.do-ams3.logos.dev.status.im/tcp/30303/p2p/16Uiu2HAmMK7PYygBtKUQ8EHp7EfaD3bCEsJrkFooK8RQ2PVpJprH"),
            QStringLiteral("/dns4/delivery-01.gc-us-central1-a.logos.dev.status.im/tcp/30303/p2p/16Uiu2HAm4S1JYkuzDKLKQvwgAhZKs9otxXqt8SCGtB4hoJP1S397"),
            QStringLiteral("/dns4/delivery-02.gc-us-central1-a.logos.dev.status.im/tcp/30303/p2p/16Uiu2HAm8Y9kgBNtjxvCnf1X6gnZJW5EGE4UwwCL3CCm55TwqBiH"),
            QStringLiteral("/dns4/delivery-01.ac-cn-hongkong-c.logos.dev.status.im/tcp/30303/p2p/16Uiu2HAm8YokiNun9BkeA1ZRmhLbtNUvcwRr64F69tYj9fkGyuEP"),
            QStringLiteral("/dns4/delivery-02.ac-cn-hongkong-c.logos.dev.status.im/tcp/30303/p2p/16Uiu2HAkvwhGHKNry6LACrB8TmEFoCJKEX29XR5dDUzk3UT3UNSE")
        };
        QJsonObject cfg{
            {"logLevel", "INFO"},
            {"mode", "Core"},
            {"preset", "logos.dev"},
            {"relay", true},
            {"entryNodes", entry}
        };
        const QString cfgJson = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));

        // Best-effort: the calls take effect server-side even when the cross-version LogosResult
        // marshals back as success=false (receiver is built against delivery v0.1.1; the platform
        // ships a newer delivery). Do NOT bail on the result — that's what left the UI stuck on
        // "initializing" while the node had in fact started.
        LogosResult c = m_logos->delivery_module.createNode(cfgJson);
        if (!c.success) log(QStringLiteral("createNode reported error (continuing): ") + c.getError());
        LogosResult st = m_logos->delivery_module.start();
        if (!st.success) log(QStringLiteral("start reported error (continuing): ") + st.getError());

        setNodeReady(true);
        log(QStringLiteral("delivery node up (logos.dev, %1 entry nodes)").arg(entry.size()));
    }

    subscribeTopic(directoryTopic());     // best-effort
    setDiscovering(true);
    if (connectionStatus() == QLatin1String("initializing"))
        setConnectionStatus(QStringLiteral("connecting"));   // unstick the status until peers arrive
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
    LogosResult r = m_logos->delivery_module.subscribe(topic);   // best-effort (LogosResult unreliable cross-version)
    if (!r.success) log(QStringLiteral("subscribe(%1) reported error (continuing): %2").arg(topic, r.getError()));
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
    // Security seam (radio #18): a station's streamUrl is attacker-controlled (anyone can announce).
    // Only let ffplay open http/https — never file:/pipe:/concat:/device/other ffmpeg protocols.
    const QString scheme = QUrl(streamUrl).scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        log("refused unsafe stream URL (only http/https): " + streamUrl);
        return QStringLiteral("unsafe_url");
    }
    m_playingUrl = streamUrl;
    setNowPlaying(stationName.isEmpty() ? streamUrl : stationName);
    const QString e = startFfplay();
    if (!e.isEmpty()) {
        m_playingUrl.clear();
        setNowPlaying(QString());
        log("playback failed: " + e);
        return e;
    }
    log("playing \"" + nowPlaying() + "\"");
    return QString();
}

QString ReceiverUiPlugin::startFfplay()
{
    killPlayer();
    const bool onion = isOnionUrl(m_playingUrl);
    if (onion) {
        const QString te = ensureTorListen();   // .onion needs a local tor SOCKS proxy
        if (!te.isEmpty()) return te;
    } else {
        killTorListen();
    }

    const QString ffplay = resolveBin(QStringLiteral("ffplay"), "RECEIVER_FFPLAY_BIN");
    QStringList ffargs;
    ffargs << "-nodisp" << "-autoexit" << "-loglevel" << "error" << "-infbuf"
           // MediaMTX gates onion HLS with a Secure cookieCheck cookie ffmpeg won't return over the
           // http onion → 302 loop → no audio. Pre-supply it (radio onion fix).
           << "-cookies" << "cookieCheck=1; path=/";
    if (listenBuffer() > 0)
        ffargs << "-live_start_index" << QString::number(-listenBuffer());   // jitter buffer (segments behind live)
    ffargs << m_playingUrl;

    QString program; QStringList args;
    if (onion) {
        // ffmpeg has no native SOCKS → route .onion playback through torsocks (LD_PRELOAD → tor SOCKS).
        program = resolveBin(QStringLiteral("torsocks"), "RECEIVER_TORSOCKS_BIN");
        args = QStringList() << ffplay << ffargs;
    } else {
        program = ffplay; args = ffargs;
    }

    m_player = new QProcess(this);
    QProcessEnvironment env = cleanSpawnEnv();
    if (onion) {
        // Lock torsocks onto OUR tor SOCKS instance (radio Senty ISSUE-4) — else it uses the compiled-in
        // 9050 default and could fail or leak via a direct connection.
        env.insert("TORSOCKS_TOR_ADDRESS", "127.0.0.1");
        env.insert("TORSOCKS_TOR_PORT", QString::number(m_listenSocksPort));
        env.insert("TORSOCKS_ISOLATE_PID", "1");
    }
    m_player->setProcessEnvironment(env);
    m_player->start(program, args);
    if (!m_player->waitForStarted(5000)) {
        const bool notFound = m_player->error() == QProcess::FailedToStart;
        qWarning() << "receiver_ui: player failed:" << m_player->errorString();
        killPlayer();
        return notFound ? QStringLiteral("ffplay/torsocks not found") : QStringLiteral("ffplay_failed");
    }
    return QString();
}

void ReceiverUiPlugin::killPlayer()
{
    if (!m_player) return;
    m_player->terminate();
    if (!m_player->waitForFinished(2000)) m_player->kill();
    m_player->deleteLater();
    m_player = nullptr;
}

QString ReceiverUiPlugin::ensureTorListen()
{
    if (m_torListen && m_torListen->state() == QProcess::Running) return QString();
    const int base = 9250;   // dedicated listener SOCKS range (avoid system tor's 9050)
    QString err;
    for (int off = 0; off < 4; ++off) {
        const int p = base + off;
        QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                      + "/receiver_ui/torlisten-" + randomHex(4);
        QString cfg;
        QTextStream s(&cfg);
        s << "SocksPort " << p << "\n"
          << "DataDirectory " << dir << "/data\n"
          << "Log notice file " << dir << "/tor.log\n";
        if (startTorProc(dir, cfg, p, err)) { m_torListenDir = dir; m_listenSocksPort = p; return QString(); }
    }
    return err.isEmpty() ? QStringLiteral("tor_listen_failed") : err;
}

void ReceiverUiPlugin::killTorListen()
{
    if (!m_torListen) return;
    m_torListen->terminate();
    if (!m_torListen->waitForFinished(2000)) m_torListen->kill();
    m_torListen->deleteLater();
    m_torListen = nullptr;
    if (!m_torListenDir.isEmpty()) { QDir(m_torListenDir).removeRecursively(); m_torListenDir.clear(); }
    m_listenSocksPort = 0;
}

bool ReceiverUiPlugin::startTorProc(QString& dir, const QString& cfg, int socksPort, QString& errOut)
{
    const QString bin = resolveBin(QStringLiteral("tor"), "RECEIVER_TOR_BIN");
    const QString dataDir = dir + "/data", torrc = dir + "/torrc";
    auto fail = [&](const QString& code) { QDir(dir).removeRecursively(); errOut = code; return false; };
    if (!QDir().mkpath(dataDir)) return fail(QStringLiteral("tor_dir_failed"));
    const QFileDevice::Permissions ownerOnly =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
    QFile::setPermissions(dir, ownerOnly);
    QFile::setPermissions(dataDir, ownerOnly);
    QFile f(torrc);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail(QStringLiteral("tor_cfg_failed"));
    f.write(cfg.toUtf8()); f.close();

    m_torListen = new QProcess(this);
    m_torListen->setProcessChannelMode(QProcess::MergedChannels);
    m_torListen->setProcessEnvironment(cleanSpawnEnv());
    m_torListen->start(bin, QStringList() << "-f" << torrc);
    if (!m_torListen->waitForStarted(5000)) {
        const bool notFound = m_torListen->error() == QProcess::FailedToStart;
        qWarning() << "receiver_ui: tor failed to start:" << m_torListen->errorString();
        m_torListen->deleteLater(); m_torListen = nullptr;
        return fail(notFound ? QStringLiteral("tor not found (install/bundle tor)") : QStringLiteral("tor_start_failed"));
    }
    if (m_torListen->waitForFinished(500)) {   // immediate exit ⇒ bad cfg / port in use
        qWarning() << "receiver_ui: tor exited immediately:" << m_torListen->readAll();
        m_torListen->deleteLater(); m_torListen = nullptr;
        return fail(QStringLiteral("tor_port_in_use"));
    }
    log(QStringLiteral("listener tor up (SOCKS %1)").arg(m_listenSocksPort));
    return true;
}

QString ReceiverUiPlugin::stopPlayback()
{
    killPlayer();
    killTorListen();
    m_playingUrl.clear();
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
