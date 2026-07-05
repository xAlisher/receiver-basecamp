#include "receiver_ui_backend.h"
#include "station_identity.h"  // #13 verify announce signatures (secp256k1)
#include "logos_sdk.h"        // generated: modules().delivery_module (Qt-typed) — #20 universal
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
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

namespace {
constexpr int    kTtlMs       = 45000;   // drop a station after 45s without a heartbeat (3 missed beats)
constexpr int    kPruneMs     = 5000;
constexpr int    kMaxPlayRetry = 2;      // auto-retries after ffplay self-exits before giving up (#12)
constexpr qint64 kStablePlayMs = 45000;  // played this long before exiting ⇒ was healthy → reset retry budget
constexpr int    kRetryBackoffMs = 1500; // brief backoff before a retry (let the reaped Tor's port free)
constexpr int    kNoAudioMs      = 35000; // #23 first stall window — long enough that a fresh cold Tor
                                          // (~25s bootstrap+connect, measured) isn't reaped before it lands.
constexpr int    kPatientMs      = 55000; // #23 per-reap patient window — a fresh Tor's rendezvous can take
                                          // anywhere from ~10s to >55s, so give each reaped Tor a full window.
constexpr int    kMaxReaps       = 3;     // #23 reap up to this many times before giving up — a LIVE station
                                          // connects within a retry or two; one window is too eager (false "unreachable").
const char* const kSettingsOrg = "logos";
const char* const kSettingsApp = "receiver_ui";
const char* const kDirTopic    = "/radio-basecamp/1/directory/json";

// File diagnostic — ui-host child stderr/qInfo is swallowed (#163), so write a timestamped trail to
// a file we can read out-of-band. TEMP instrumentation for the spinner/discovery investigation.
void diag(const QString& m) {
    QFile f(QStringLiteral("/tmp/receiver-diag.log"));
    if (f.open(QIODevice::Append | QIODevice::WriteOnly)) {
        f.write((QDateTime::currentDateTime().toString("HH:mm:ss.zzz") + "  " + m + "\n").toUtf8());
        f.close();
    }
}

bool isOnionUrl(const QString& url) { return QUrl(url).host().endsWith(QLatin1String(".onion")); }

QString randomHex(int bytes) {
    QString s;
    for (int i = 0; i < bytes; ++i) s += QString("%1").arg(QRandomGenerator::global()->bounded(256), 2, 16, QChar('0'));
    return s;
}

// Resolve a runtime helper: env override → bare name on PATH (option 1: tor/ffmpeg installed per-OS).
// NB: deliberately no dladdr/"next to the .so" lookup — that pulls dladdr@GLIBC_2.34, which the
// AppImage's older bundled glibc can't resolve, so the plugin fails to dlopen (sidebar spinner).
QString resolveBin(const QString& name, const char* envVar) {
    const QString env = qEnvironmentVariable(envVar);
    return env.isEmpty() ? name : env;
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

ReceiverUiBackend::ReceiverUiBackend(QObject* parent)
    : ReceiverUiSimpleSource(parent)
{
    // cheap, no IPC — restore persisted settings + start the station-TTL prune loop. Delivery wiring
    // waits for modules() (onContextReady).
    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    setListenBuffer(qBound(2, s.value(QStringLiteral("listenBuffer"), 20).toInt(), 60));  // #11 ceiling 60s
    setHideCache(s.value(QStringLiteral("hideCache"), false).toBool());

    m_pruneTimer = new QTimer(this);
    m_pruneTimer->setInterval(kPruneMs);
    QObject::connect(m_pruneTimer, &QTimer::timeout, this, &ReceiverUiBackend::pruneStations);
    m_pruneTimer->start();
}

ReceiverUiBackend::~ReceiverUiBackend()
{
    stopPlayback();
}

void ReceiverUiBackend::onContextReady()
{
    // modules() is now live. Defer discovery ~2.5s (delivery_module just finished init). Do NOT subscribe
    // to events yet — event subscription must happen AFTER the blocking createNode() returns, else the
    // connectionStateChanged that delivery emits DURING createNode reenters this single ui-host thread
    // while it's blocked on createNode's reply → deadlock (sync-ipc-reentrancy). See wireDeliveryEvents().
    diag(QStringLiteral("onContextReady: modules() wired"));
    setPublicTopic(directoryTopic());   // #44 expose the public directory topic for the list filter
    QTimer::singleShot(2500, this, [this]{ diag(QStringLiteral("fire deferred startDiscovery")); startDiscovery(); });
}

void ReceiverUiBackend::wireDeliveryEvents()
{
    if (m_eventsWired) return;
    m_eventsWired = true;
    // messageReceived → announce ingest. connectionStateChanged → the status pill (d[0] =
    // "Connected"|"PartiallyConnected"|"Disconnected"). Typed modules().delivery_module.on(name, cb).
    modules().delivery_module.on("messageReceived", [this](const QVariantList& d) {
        // A received announce PROVES the node is connected + discovering. The connectionStateChanged
        // "Connected" event can fire during the fire-and-forget startup window BEFORE we subscribe (#20),
        // so it's missed and the pill sticks at "connecting" even while announces flow. Upgrade it here.
        if (connectionStatus() == QLatin1String("connecting") || connectionStatus() == QLatin1String("initializing"))
            setConnectionStatus(QStringLiteral("Connected"));
        diag(QStringLiteral("on messageReceived: d.size=%1").arg(d.size()));
        for (const QVariant& v : d) ingestAnnounce(v);
    });
    modules().delivery_module.on("connectionStateChanged", [this](const QVariantList& d) {
        if (!d.isEmpty()) {
            setConnectionStatus(d[0].toString());
            diag(QStringLiteral("on connectionStateChanged -> %1").arg(d[0].toString()));
        }
    });
    diag(QStringLiteral("wireDeliveryEvents: subscribed (post-createNode)"));
}

QString ReceiverUiBackend::startDiscovery()
{
    if (!isContextReady()) return QStringLiteral("context_not_ready");

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

        // #20 FIX — FIRE-AND-FORGET async. Headless findings:
        //  - SYNC createNode() deadlocks: blocks the single ui-host thread; delivery's reply needs that
        //    (now-blocked) event loop → never returns.
        //  - ASYNC createNodeAsync's *reply callback* also doesn't get delivered here — BUT the async
        //    *send* IS processed server-side (delivery logs "context created successfully").
        // So don't rely on the reply/callback at all: fire createNode, then fire start after a short
        // delay (so the context exists — the reply that would sequence them isn't coming), then subscribe.
        // The node comes up from the sends; discovery rides delivery's event-PUSH (.on) path.
        diag(QStringLiteral("startDiscovery: createNodeAsync (fire-and-forget — sync deadlocks + async reply undelivered, #20)"));
        modules().delivery_module.createNodeAsync(cfgJson,
            [this](LogosResult r){ diag(QStringLiteral("createNodeAsync cb (if ever): ok=%1").arg(r.success)); }, Timeout());
        setNodeReady(true);
        const int entryCount = entry.size();
        QTimer::singleShot(3000, this, [this, entryCount]{
            diag(QStringLiteral("fire startAsync + subscribe (context should exist by now)"));
            modules().delivery_module.startAsync(
                [this](LogosResult r){ diag(QStringLiteral("startAsync cb (if ever): ok=%1").arg(r.success)); }, Timeout());
            log(QStringLiteral("delivery node up (logos.dev, %1 entry nodes, async fire-and-forget)").arg(entryCount));
            subscribeTopic(directoryTopic());
            setDiscovering(true);
            if (connectionStatus() == QLatin1String("initializing"))
                setConnectionStatus(QStringLiteral("connecting"));
            log("discovering on " + directoryTopic());
            wireDeliveryEvents();
        });
        return QString();   // node bring-up continues on the timer above
    }

    // Already node-ready (re-entry, e.g. QML re-calls startDiscovery): just (re)subscribe + wire.
    subscribeTopic(directoryTopic());
    setDiscovering(true);
    wireDeliveryEvents();
    return QString();
}

QString ReceiverUiBackend::stopDiscovery()
{
    if (!isContextReady()) return QStringLiteral("context_not_ready");
    for (const QString& t : m_subscribed)
        modules().delivery_module.unsubscribeAsync(t, [](LogosResult){}, Timeout());  // async — sync would deadlock (#20)
    m_subscribed.clear();
    setDiscovering(false);
    log("discovery stopped");
    return QString();
}

QString ReceiverUiBackend::addTopic(QString topic)
{
    topic = topic.trimmed();
    if (topic.isEmpty()) return QStringLiteral("empty topic");
    if (!subscribeTopic(topic)) return QStringLiteral("subscribe failed");
    log("switched to topic " + topic);
    return QString();
}

bool ReceiverUiBackend::subscribeTopic(const QString& topic)
{
    if (!isContextReady()) return false;
    if (m_subscribed.contains(topic)) return true;
    // async — sync subscribe() would deadlock the ui-host event loop like createNode (#20)
    modules().delivery_module.subscribeAsync(topic, [](LogosResult){}, Timeout());
    m_subscribed.insert(topic);
    return true;
}

void ReceiverUiBackend::ingestAnnounce(const QVariant& payload)
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
    // #40 now-playing: attacker-controllable announce data — cap length + strip control chars before render
    s.nowPlaying = o.value(QStringLiteral("nowPlaying")).toString()
                     .remove(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]"))).left(120);
    s.lastSeenMs = QDateTime::currentMSecsSinceEpoch();

    // #13 verify station identity. v:2 carries pubkey + sig over the canonical (sig-less) announce bytes;
    // an invalid signature is a forgery/tamper → DROP. v:1 (unsigned) is kept as anonymous/unverified.
    const int ver = o.value(QStringLiteral("v")).toInt(1);
    const QString pubkey = o.value(QStringLiteral("pubkey")).toString();
    const QString sig    = o.value(QStringLiteral("sig")).toString();
    if (ver >= 2 && !pubkey.isEmpty() && !sig.isEmpty()) {
        QJsonObject signedObj = o;
        signedObj.remove(QStringLiteral("sig"));
        const QByteArray canon = QJsonDocument(signedObj).toJson(QJsonDocument::Compact);
        if (!StationIdentity::verify(pubkey, sig, canon)) {
            log("dropped forged announce for \"" + s.name + "\" (bad signature)");
            return;
        }
        s.pubkey      = pubkey;
        s.fingerprint = StationIdentity::fingerprint(pubkey);
        s.verified    = true;
    }

    const QString key = s.topic + "|" + s.name;
    const bool isNew = !m_stations.contains(key);
    m_stations.insert(key, s);
    if (isNew) {
        log("discovered \"" + s.name + "\"");
        prewarm(s.streamUrl);   // #28 warm this onion's rendezvous circuit on discovery → fast first play (no hover)
    }
    publishStations();
}

void ReceiverUiBackend::pruneStations()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (auto it = m_stations.begin(); it != m_stations.end(); ) {
        if (now - it.value().lastSeenMs > kTtlMs) { it = m_stations.erase(it); changed = true; }
        else ++it;
    }
    if (changed) publishStations();
}

void ReceiverUiBackend::publishStations()
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
        o["nowPlaying"] = s.nowPlaying;   // #40
        o["verified"]   = s.verified;     // #13 identity
        o["pubkey"]     = s.pubkey;
        o["fingerprint"] = s.fingerprint;
        o["uptimeS"]   = (double)((now - s.lastSeenMs) / 1000);
        arr.append(o);
    }
    setStationsJson(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

QString ReceiverUiBackend::play(QString streamUrl, QString stationName)
{
    // Security seam (radio #18): a station's streamUrl is attacker-controlled (anyone can announce).
    // Only let ffplay open http/https — never file:/pipe:/concat:/device/other ffmpeg protocols.
    const QString scheme = QUrl(streamUrl).scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        log("refused unsafe stream URL (only http/https): " + streamUrl);
        return QStringLiteral("unsafe_url");
    }
    m_playingUrl = streamUrl;
    m_playAttempt = 0;   // fresh user-initiated play → reset the auto-retry budget (#12)
    m_reapCount = 0;   // #23 fresh play → reset the reap budget
    setNowPlaying(stationName.isEmpty() ? streamUrl : stationName);
    const QString e = startFfplay();
    if (!e.isEmpty()) {
        m_playingUrl.clear();
        setNowPlaying(QString());
        log("playback failed: " + e);
        return e;
    }
    log("connecting to \"" + nowPlaying() + "\"");   // not "playing" yet — audio starts on the clock signal
    return QString();
}

QString ReceiverUiBackend::prewarm(QString streamUrl)
{
    // #30 (was #26/#28/#29 — the warm SOCKET couldn't beat Tor's rendezvous variability, because the warm
    // connection IS the rendezvous). Keep only the reliable win: spawn the listener Tor early on select/
    // discovery so its ~11s bootstrap is done before Play. Fast recovery is owned by the multi-reap watchdog.
    if (!isOnionUrl(streamUrl)) return QString();       // only .onion needs a listener Tor
    if (!m_playingUrl.isEmpty()) return QString();      // already playing
    if (m_torListen && m_torListen->state() == QProcess::Running) return QString();   // Tor already up
    log(QStringLiteral("Prepping Tor for playback…"));
    ensureTorListen();   // spawn + bootstrap now so Play doesn't wait for it
    return QString();
}

QString ReceiverUiBackend::startFfplay()
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
           << "-stats"   // #23 periodic decode status line on stderr → the no-audio watchdog reads it
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
    // A live radio stream never ends normally, so ffplay self-exiting means the stream dropped (cold HLS,
    // onion rendezvous, tor). Watch for it → reap the listener Tor + retry (#12). killPlayer() disconnects
    // this first, so an intentional stop/restart never triggers a retry.
    m_playStartMs = QDateTime::currentMSecsSinceEpoch();
    connect(m_player, &QProcess::finished, this, [this] {
        retryOrStopPlayback(QDateTime::currentMSecsSinceEpoch() - m_playStartMs);
    });

    // #23 no-audio watchdog: ffplay can connect but buffer SILENT (Tor rendezvous cold-start) without
    // exiting (-infbuf), so the finished-handler never fires. Watch ffplay's -stats output for a decode
    // status line ("aq="); if none arrives within kNoAudioMs, the stream is stuck → reap Tor + retry.
    m_audioFlowing = false;
    setPlaybackLive(false);
    setBuffering(false);
    connect(m_player, &QProcess::readyReadStandardError, this, [this] {
        if (m_audioFlowing && playbackLive()) return;   // both signals seen — nothing left to watch
        const QString e = QString::fromLatin1(m_player->readAllStandardError());
        // (1) WATCHDOG signal — a NON-ZERO audio queue means ffplay pulled real stream bytes, so the
        // connect works (not stuck). The bare initial "aq= 0KB" line prints even with no data, so require
        // aq>0 or the watchdog false-cancels (observed: 0.2% CPU, no sink-input, yet "audio flowing"). #23
        if (!m_audioFlowing) {
            static const QRegularExpression aqRe(QStringLiteral("aq=\\s*([0-9]+)KB"));
            auto it = aqRe.globalMatch(e);
            while (it.hasNext()) {
                if (it.next().captured(1).toInt() > 0) {
                    m_audioFlowing = true;
                    setBuffering(true);   // #9 bytes arriving → UI "Caching" (was "Connecting")
                    if (m_watchdog) m_watchdog->stop();
                    diag(QStringLiteral("audio buffering (aq>0) — connect works"));
                    break;
                }
            }
        }
        // (2) TRUE-PLAYING signal — ffplay's -stats leading field is the master clock: "nan" while
        // connecting/buffering, a real number the instant audio is actually OUTPUT (measured: nan→4395.23
        // exactly at first sample). Drives the UI so "Playing" == sound, not a countdown. #9
        if (!playbackLive()) {
            static const QRegularExpression liveRe(QStringLiteral("(?:^|[\\r\\n])\\s*[0-9]+\\.[0-9]+\\s+(?:M-A|A-V)"));
            if (liveRe.match(e).hasMatch()) {
                setPlaybackLive(true);
                log("▶ playing \"" + nowPlaying() + "\"");   // the honest "playing" — audio is actually out
                diag(QStringLiteral("playback LIVE (ffplay master clock ticking) — audio is out"));
            }
        }
    });
    if (!m_watchdog) {
        m_watchdog = new QTimer(this);
        m_watchdog->setSingleShot(true);
        QObject::connect(m_watchdog, &QTimer::timeout, this, &ReceiverUiBackend::onNoAudioWatchdog);
    }
    m_watchdog->start(m_reapCount > 0 ? kPatientMs : kNoAudioMs);  // patient window after the first reap
    return QString();
}

void ReceiverUiBackend::onNoAudioWatchdog()
{
    if (m_playingUrl.isEmpty() || m_audioFlowing) return;   // stopped, or audio came through
    if (m_reapCount >= kMaxReaps) {
        // Reaped kMaxReaps times, each with a full patient window, still no stream bytes — the broadcaster
        // is very likely offline (a LIVE station connects within a retry or two). Stop; the user can retry.
        diag(QStringLiteral("no-audio watchdog: still no data after %1 reaps — stopping").arg(kMaxReaps));
        killPlayer(); killTorListen();
        m_playingUrl.clear(); setNowPlaying(QString());
        log(QStringLiteral("couldn't reach the station over Tor — its onion may be down; try again"));
        return;
    }
    // No stream bytes yet. The listener Tor may be stale (cached a failed HS-descriptor lookup when the
    // broadcaster was briefly dark — measured: same ffplay is 9s on a fresh Tor vs >125s on the stale one),
    // or the fresh Tor's rendezvous is just slow. Reap for a clean circuit and wait a full patient window;
    // repeat up to kMaxReaps before concluding the station is down — one window is too eager.
    ++m_reapCount;
    diag(QStringLiteral("no-audio watchdog fired — reap Tor %1/%2, patient retry").arg(m_reapCount).arg(kMaxReaps));
    log(m_reapCount == 1 ? QStringLiteral("no sound yet — reconnecting")
                         : QStringLiteral("still connecting…"));
    killPlayer();          // disconnects the finished handler so the reap doesn't also trip #21
    killTorListen();       // drop this Tor → ensureTorListen() spawns a fresh one on the retry
    QTimer::singleShot(kRetryBackoffMs, this, [this] {
        if (m_playingUrl.isEmpty()) return;   // user stopped during the backoff
        const QString e = startFfplay();
        if (!e.isEmpty()) { m_playingUrl.clear(); setNowPlaying(QString()); }
    });
}

void ReceiverUiBackend::killPlayer()
{
    if (m_watchdog) m_watchdog->stop();             // #23 cancel the no-audio watchdog for this player
    setPlaybackLive(false);                         // #9 no player → audio is not out (UI leaves "Playing")
    setBuffering(false);                            // #9 no player → not buffering either
    if (!m_player) return;
    disconnect(m_player, nullptr, this, nullptr);   // intentional kill — don't fire the retry handler (#12)
    m_player->terminate();
    if (!m_player->waitForFinished(2000)) m_player->kill();
    m_player->deleteLater();
    m_player = nullptr;
}

void ReceiverUiBackend::retryOrStopPlayback(qint64 ranMs)
{
    if (m_player) { m_player->deleteLater(); m_player = nullptr; }
    if (m_playingUrl.isEmpty()) return;   // already stopped by the user

    if (ranMs > kStablePlayMs) m_playAttempt = 0;   // was healthy for a while → fresh retry budget
    if (m_playAttempt < kMaxPlayRetry) {
        ++m_playAttempt;
        // Reap the listener Tor: a stale one caches a failed onion descriptor lookup and keeps returning
        // "no route", so a FRESH Tor (respawned by ensureTorListen on retry) is the actual fix (#12).
        if (isOnionUrl(m_playingUrl)) killTorListen();
        diag(QStringLiteral("player exited after %1ms — reap Tor + retry %2/%3").arg(ranMs).arg(m_playAttempt).arg(kMaxPlayRetry));
        log(QStringLiteral("stream dropped — retrying (%1/%2)").arg(m_playAttempt).arg(kMaxPlayRetry));
        QTimer::singleShot(kRetryBackoffMs, this, [this] {
            if (m_playingUrl.isEmpty()) return;   // user stopped during the backoff
            const QString e = startFfplay();
            if (!e.isEmpty()) { m_playingUrl.clear(); setNowPlaying(QString()); log("retry failed: " + e); }
        });
        return;
    }
    diag(QStringLiteral("player exited after %1ms — giving up after %2 retries").arg(ranMs).arg(kMaxPlayRetry));
    m_playingUrl.clear();
    setNowPlaying(QString());
    log(QStringLiteral("playback stopped — stream unavailable after %1 retries").arg(kMaxPlayRetry));
}

QString ReceiverUiBackend::ensureTorListen()
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
        if (startTorProc(dir, cfg, p, err)) {
            m_torListenDir = dir; m_listenSocksPort = p;
            setTorStatus(QStringLiteral("booting"));   // #37
            if (!m_torPoll) { m_torPoll = new QTimer(this); connect(m_torPoll, &QTimer::timeout, this, &ReceiverUiBackend::pollTorStatus); }
            m_torPoll->start(1000);
            return QString();
        }
    }
    return err.isEmpty() ? QStringLiteral("tor_listen_failed") : err;
}

void ReceiverUiBackend::killTorListen()
{
    if (!m_torListen) return;
    m_torListen->terminate();
    if (!m_torListen->waitForFinished(2000)) m_torListen->kill();
    m_torListen->deleteLater();
    m_torListen = nullptr;
    if (!m_torListenDir.isEmpty()) { QDir(m_torListenDir).removeRecursively(); m_torListenDir.clear(); }
    m_listenSocksPort = 0;
    if (m_torPoll) m_torPoll->stop();       // #37
    setTorStatus(QStringLiteral("off"));
}

void ReceiverUiBackend::pollTorStatus()
{
    // #37 surface the listener Tor's health from the bootstrap log it already writes.
    if (!m_torListen) { setTorStatus(QStringLiteral("off")); if (m_torPoll) m_torPoll->stop(); return; }
    if (m_torListen->state() != QProcess::Running) { setTorStatus(QStringLiteral("failed")); if (m_torPoll) m_torPoll->stop(); return; }
    QFile f(m_torListenDir + "/tor.log");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;   // not written yet — stay "booting"
    const QString log = QString::fromLatin1(f.readAll()); f.close();
    static const QRegularExpression re(QStringLiteral("Bootstrapped ([0-9]+)%"));
    int pct = -1;
    auto it = re.globalMatch(log);
    while (it.hasNext()) pct = it.next().captured(1).toInt();     // last bootstrap line wins
    if (pct >= 100) { setTorStatus(QStringLiteral("ready")); if (m_torPoll) m_torPoll->stop(); }
    else setTorStatus(QStringLiteral("booting"));
}

bool ReceiverUiBackend::startTorProc(QString& dir, const QString& cfg, int socksPort, QString& errOut)
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
    log(QStringLiteral("listener tor up (SOCKS %1)").arg(socksPort));   // #27 the real port — m_listenSocksPort isn't assigned until ensureTorListen returns
    return true;
}

QString ReceiverUiBackend::stopPlayback()
{
    killPlayer();
    killTorListen();
    m_playingUrl.clear();
    if (!nowPlaying().isEmpty()) { setNowPlaying(QString()); log("playback stopped"); }
    return QString();
}

QString ReceiverUiBackend::setBuffer(int sec)
{
    sec = qBound(2, sec, 60);   // #11 ceiling 60s
    setListenBuffer(sec);   // generated PROP setter → auto-syncs to QML
    QSettings{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)}
        .setValue(QStringLiteral("listenBuffer"), sec);
    return QString();
}

QString ReceiverUiBackend::setCacheHidden(bool on)
{
    setHideCache(on);       // generated PROP setter → auto-syncs to QML
    QSettings{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)}
        .setValue(QStringLiteral("hideCache"), on);
    if (on) clearCache();   // turning it on wipes whatever is already cached
    log(on ? "hide cache: ON (stream cache suppressed + cleared)" : "hide cache: OFF");
    return QString();
}

QString ReceiverUiBackend::clearCache()
{
    const QString dir = cacheDir();
    if (dir.isEmpty()) return QString();
    QDir d(dir);
    if (d.exists()) d.removeRecursively();
    log("cache cleared");
    return QString();
}

QString ReceiverUiBackend::cacheDir() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) return QString();
    return base + QStringLiteral("/receiver_ui");
}

QString ReceiverUiBackend::directoryTopic() const
{
    return QString::fromLatin1(kDirTopic);
}

void ReceiverUiBackend::log(const QString& line)
{
    qInfo() << "receiver_ui:" << line;
    emit activity(line);
}
