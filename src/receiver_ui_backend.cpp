#include "receiver_ui_backend.h"
#include "station_identity.h"  // #13 verify announce signatures (secp256k1)
#include "station_crypto.h"    // #69 private streams: hash(Title+Pass) topic + decrypt with Pass
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
#include <QCoreApplication>
#include <QStandardPaths>
#include <QTextStream>
#include <csignal>   // #52 kill()/SIGKILL to reap orphaned tor listeners by PID
#include <QTimer>
#include <QUrl>
#if defined(__APPLE__)
#include <mach-o/dyld.h>   // #78 _dyld_image_count/_dyld_get_image_name → module dir (no dladdr)
#include <cstring>         // std::strstr
#endif

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

// #75 Find this module's install dir (…/plugins/receiver_ui) so we can prefer the self-contained
// binaries bundled under <moduleDir>/bin/ (ffplay/tor/privoxy) over anything the user installed.
// NB: deliberately NOT dladdr — that pulls dladdr@GLIBC_2.34, which the AppImage's older bundled
// glibc can't resolve, so the plugin would fail to dlopen (sidebar spinner). We read the already-
// mapped plugin's path out of /proc/self/maps (Linux: only fopen/fgets, ancient symbols) or the
// dyld image list (macOS) — both self-contained, both cheap, both run at construction.
QString findModuleDir() {
#if defined(__linux__)
    QFile f(QStringLiteral("/proc/self/maps"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // NB: /proc/self/maps is a virtual file that reports size 0 → QFile::atEnd() is true immediately,
        // so a `while (!f.atEnd())` loop never runs. Read via readLine() until it returns empty (real EOF).
        QByteArray line;
        while (!(line = f.readLine()).isEmpty()) {
            if (line.contains("receiver_ui_plugin.so") || line.contains("receiver_ui_replica_factory.so")) {
                const int slash = line.indexOf('/');
                if (slash >= 0)
                    return QFileInfo(QString::fromUtf8(line.mid(slash)).trimmed()).absolutePath();
            }
        }
    }
#elif defined(__APPLE__)
    // macOS: walk the dyld image list for the loaded plugin dylib and take its dir. Not dladdr (same
    // GLIBC_2.34 caution as Linux) — _dyld_get_image_name is a plain libSystem call, always present.
    for (uint32_t i = 0, n = _dyld_image_count(); i < n; ++i) {
        const char* name = _dyld_get_image_name(i);
        if (name && (std::strstr(name, "receiver_ui_plugin") || std::strstr(name, "receiver_ui_replica_factory")))
            return QFileInfo(QString::fromUtf8(name)).absolutePath();
    }
#endif
    return QString();   // unknown module dir → resolveBin falls back to env/PATH (the deps-card path)
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
    m_moduleDir = findModuleDir();   // #75 locate <…>/plugins/receiver_ui so bundledBin() can prefer bin/*
    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    setListenBuffer(qBound(2, s.value(QStringLiteral("listenBuffer"), 20).toInt(), 60));  // #11 ceiling 60s
    setHideCache(s.value(QStringLiteral("hideCache"), false).toBool());
    loadPins();   // #14 restore pinned stations (persist across module reload)
    publishDeps();   // #55 preflight the playback helpers so the welcome card is correct on first paint

    m_pruneTimer = new QTimer(this);
    m_pruneTimer->setInterval(kPruneMs);
    QObject::connect(m_pruneTimer, &QTimer::timeout, this, &ReceiverUiBackend::pruneStations);
    m_pruneTimer->start();
}

ReceiverUiBackend::~ReceiverUiBackend()
{
    stopPlayback();
}

QString ReceiverUiBackend::checkDeps()
{
    publishDeps();
    return QString();   // "" = success — Re-check just refreshes the depsJson PROP the card binds to
}

// #75 A helper bundled next to the plugin at <moduleDir>/bin/<name> (self-contained, $ORIGIN-rpath'd).
// "" if we don't know the module dir (macOS pre-darwin-wiring) or the bundle isn't present.
QString ReceiverUiBackend::bundledBin(const QString& name) const
{
    if (m_moduleDir.isEmpty()) return QString();
    const QFileInfo fi(m_moduleDir + QStringLiteral("/bin/") + name);
    return (fi.exists() && fi.isExecutable()) ? fi.absoluteFilePath() : QString();
}

// #75 Resolve a runtime helper. Priority: explicit RECEIVER_*_BIN override (debugging/BYO) →
// the self-contained bundled binary (zero-install) → bare name on PATH (legacy: user-installed).
// Bundled binaries are $ORIGIN-rpath'd, so cleanSpawnEnv()'s dropped LD_LIBRARY_PATH doesn't hurt them.
QString ReceiverUiBackend::resolveBin(const QString& name, const char* envVar) const
{
    const QString env = qEnvironmentVariable(envVar);
    if (!env.isEmpty()) return env;
    const QString bundled = bundledBin(name);
    if (!bundled.isEmpty()) return bundled;
    return name;
}

// #55 Detect the external playback helpers on PATH and publish the JSON the first-launch card reads.
// tor + ffplay are needed on every OS; the SOCKS shim differs (torsocks on Linux, privoxy on macOS —
// torsocks' LD_PRELOAD shim is SIP-blocked on mac). Honors the RECEIVER_*_BIN overrides: an absolute
// override path counts as present iff it exists+executable, otherwise we search PATH (same resolution
// QProcess uses for a bare program name). We never auto-install (needs root / is distro-specific) — we
// only surface the exact command for the user to paste.
void ReceiverUiBackend::publishDeps()
{
    struct Req { const char* bin; const char* env; const char* pkg; };
#ifdef __APPLE__
    const QString os = QStringLiteral("macos");
    const QList<Req> reqs = {
        { "tor",     "RECEIVER_TOR_BIN",     "tor"     },
        { "ffplay",  "RECEIVER_FFPLAY_BIN",  "ffmpeg"  },
        { "privoxy", "RECEIVER_PRIVOXY_BIN", "privoxy" },
    };
#else
    const QString os = QStringLiteral("linux");
    const QList<Req> reqs = {
        { "tor",      "RECEIVER_TOR_BIN",      "tor"      },
        { "ffplay",   "RECEIVER_FFPLAY_BIN",   "ffmpeg"   },
        { "torsocks", "RECEIVER_TORSOCKS_BIN", "torsocks" },
    };
#endif

    // #57 Known install dirs to probe even when they're OFF the (minimal) GUI PATH — this is how we
    // distinguish "installed but invisible" (→ launchctl setenv) from "truly missing" (→ install it).
#ifdef __APPLE__
    const QStringList probeDirs = {
        QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/opt/homebrew/sbin"),
        QStringLiteral("/usr/local/bin"),    QStringLiteral("/usr/local/sbin"),
        QDir::homePath() + QStringLiteral("/.nix-profile/bin"),
    };
#else
    const QStringList probeDirs = {
        QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/bin"), QStringLiteral("/usr/local/sbin"),
        QDir::homePath() + QStringLiteral("/.nix-profile/bin"),
        QStringLiteral("/run/current-system/sw/bin"), QStringLiteral("/snap/bin"),
    };
#endif
    auto findInDirs = [&](const QString& name) -> QString {
        for (const QString& d : probeDirs) {
            const QFileInfo fi(d + QLatin1Char('/') + name);
            if (fi.exists() && fi.isExecutable()) return fi.absoluteFilePath();
        }
        return QString();
    };

    // #57 which package manager to recommend for the truly-missing helpers
    QString pkgMgr;
#ifdef __APPLE__
    if (QFileInfo::exists(QStringLiteral("/opt/homebrew/bin/brew"))
        || QFileInfo::exists(QStringLiteral("/usr/local/bin/brew")))               pkgMgr = QStringLiteral("brew");
    else if (QFileInfo::exists(QDir::homePath() + QStringLiteral("/.nix-profile"))
        || !QStandardPaths::findExecutable(QStringLiteral("nix")).isEmpty())        pkgMgr = QStringLiteral("nix");
    else                                                                           pkgMgr = QStringLiteral("none");
#else
    if (!QStandardPaths::findExecutable(QStringLiteral("apt")).isEmpty()
        || !QStandardPaths::findExecutable(QStringLiteral("apt-get")).isEmpty())    pkgMgr = QStringLiteral("apt");
    else if (!QStandardPaths::findExecutable(QStringLiteral("dnf")).isEmpty())      pkgMgr = QStringLiteral("dnf");
    else if (!QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty())   pkgMgr = QStringLiteral("pacman");
    else                                                                           pkgMgr = QStringLiteral("apt");
#endif

    QJsonArray items;
    QStringList missingPkgs;
    QStringList setenvLines;
    bool ok = true, needsRelaunch = false;

    for (const Req& r : reqs) {
        const QString name = QString::fromLatin1(r.bin);
        const QString envOverride = qEnvironmentVariable(r.env);
        QString state, path;

        if (!envOverride.isEmpty() && QFileInfo(envOverride).isAbsolute()) {
            const QFileInfo fi(envOverride);                                        // explicit RECEIVER_*_BIN path
            if (fi.exists() && fi.isExecutable()) { state = QStringLiteral("present"); path = fi.absoluteFilePath(); }
            else                                    state = QStringLiteral("missing");   // override points at a dead path
        }
        if (state.isEmpty()) {
            const QString bundled = bundledBin(name);   // #75 self-contained helper shipped in the module
            if (!bundled.isEmpty()) { state = QStringLiteral("present"); path = bundled; }
        }
        // #75 torsocks ships as an LD_PRELOAD .so (no wrapper) — count the bundled libtorsocks.so as present
        if (state.isEmpty() && name == QLatin1String("torsocks") && !m_moduleDir.isEmpty()) {
            const QString ts = m_moduleDir + QStringLiteral("/bin/libtorsocks.so");
            if (QFileInfo::exists(ts)) { state = QStringLiteral("present"); path = ts; }
        }
        if (state.isEmpty()) {
            const QString onPath = QStandardPaths::findExecutable(name);         // the (minimal) GUI PATH
            if (!onPath.isEmpty()) { state = QStringLiteral("present"); path = onPath; }
            else {
                const QString off = findInDirs(name);                           // probe known dirs off-PATH
                if (!off.isEmpty()) { state = QStringLiteral("found_offpath"); path = off; }
                else                  state = QStringLiteral("missing");
            }
        }

        QJsonObject o;
        o.insert(QStringLiteral("name"),  name);
        o.insert(QStringLiteral("state"), state);
        if (!path.isEmpty()) o.insert(QStringLiteral("path"), path);

        if (state == QLatin1String("found_offpath")) {
#ifdef __APPLE__
            const QString cmd = QStringLiteral("launchctl setenv %1 %2").arg(QString::fromLatin1(r.env), path);
#else
            const QString cmd = QStringLiteral("export %1=%2").arg(QString::fromLatin1(r.env), path);
#endif
            o.insert(QStringLiteral("setenvCmd"), cmd);
            setenvLines << cmd;
            needsRelaunch = true; ok = false;
        } else if (state == QLatin1String("missing")) {
            const QString pkg = QString::fromLatin1(r.pkg);
            o.insert(QStringLiteral("pkg"), pkg);
            if (!missingPkgs.contains(pkg)) missingPkgs.append(pkg);
            ok = false;
        }
        items.append(o);
    }

    // #57 install command for the truly-missing helpers, tailored to the detected package manager
    QString installCmd;
    if (!missingPkgs.isEmpty()) {
        const QString pkgs = missingPkgs.join(QLatin1Char(' '));
        // #68 brew install must be non-interactive AND self-contained: HOMEBREW_NO_INSTALL_UPGRADE=1
        // skips the dependency-upgrade "[y/n]" confirm, HOMEBREW_NO_AUTO_UPDATE=1 the auto-update noise,
        // and "</dev/null" detaches stdin so no following pasted line (a launchctl setenv) is read as a
        // prompt answer. Keep this brew invocation byte-identical with Main.qml macFastPath + README.
        if (pkgMgr == QLatin1String("brew"))        installCmd = QStringLiteral("HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_UPGRADE=1 brew install ") + pkgs + QStringLiteral(" </dev/null");
        else if (pkgMgr == QLatin1String("nix")) {
            QStringList nixed; for (const QString& p : missingPkgs) nixed << (QStringLiteral("nixpkgs#") + p);
            installCmd = QStringLiteral("nix profile install ") + nixed.join(QLatin1Char(' '));
        }
        else if (pkgMgr == QLatin1String("none"))   installCmd = QStringLiteral("# install Homebrew from https://brew.sh, then:\nHOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_UPGRADE=1 brew install ") + pkgs + QStringLiteral(" </dev/null");
        else if (pkgMgr == QLatin1String("dnf"))    installCmd = QStringLiteral("sudo dnf install -y ") + pkgs;
        else if (pkgMgr == QLatin1String("pacman")) installCmd = QStringLiteral("sudo pacman -S --noconfirm ") + pkgs;
        else                                        installCmd = QStringLiteral("sudo apt install -y ") + pkgs;
    }

    QJsonObject rootObj;
    rootObj.insert(QStringLiteral("ok"),            ok);
    rootObj.insert(QStringLiteral("os"),            os);
    rootObj.insert(QStringLiteral("pkgMgr"),        pkgMgr);
    rootObj.insert(QStringLiteral("items"),         items);
    rootObj.insert(QStringLiteral("installCmd"),    installCmd);
    rootObj.insert(QStringLiteral("setenvBlock"),   setenvLines.join(QLatin1Char('\n')));
    rootObj.insert(QStringLiteral("needsRelaunch"), needsRelaunch);
    const QString json = QString::fromUtf8(QJsonDocument(rootObj).toJson(QJsonDocument::Compact));
    diag(QStringLiteral("publishDeps -> %1").arg(json));   // #55/#57 the payload the card renders
    setDepsJson(json);
}

void ReceiverUiBackend::onContextReady()
{
    // modules() is now live. Defer discovery ~2.5s (delivery_module just finished init). Do NOT subscribe
    // to events yet — event subscription must happen AFTER the blocking createNode() returns, else the
    // connectionStateChanged that delivery emits DURING createNode reenters this single ui-host thread
    // while it's blocked on createNode's reply → deadlock (sync-ipc-reentrancy). See wireDeliveryEvents().
    diag(QStringLiteral("onContextReady: modules() wired"));
    StationCrypto::init();   // #69 libsodium — init before any private-stream topic/key/decrypt op
    diag(QStringLiteral("private-stream crypto selftest: %1").arg(StationCrypto::selfTest() ? "OK" : "FAIL"));
    publishDeps();   // #55 re-publish now the QML replica is connected (constructor-time set can precede remoting)
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

// #69 A private stream: from the shared Title + Pass, derive the SAME topic the broadcaster derived
// (hash(Title+Pass)), subscribe to it, and remember the AEAD key + segment so ingestAnnounce can
// decrypt its (encrypted) announces. A listener needs BOTH Title and Pass; a relay node sees only a
// random-hash topic it can't identify or decode. Symmetric to booth#66 (see docs/private-stream-protocol.md).
QString ReceiverUiBackend::addPrivateStream(QString title, QString pass)
{
    title = title.trimmed();
    if (title.isEmpty() || pass.isEmpty()) return QStringLiteral("need both a station name and a passphrase");
    StationCrypto::init();
    const QString topic = StationCrypto::deriveTopic(title, pass);
    const QString seg   = StationCrypto::deriveTopicSegment(title, pass);
    const QByteArray key = StationCrypto::deriveKey(title, pass);
    if (key.size() != 32) return QStringLiteral("key derivation failed");
    if (!subscribeTopic(topic)) return QStringLiteral("subscribe failed");
    m_privStreams.insert(topic, PrivStream{ seg, key });   // remembered so the announce decrypts
    log("added private stream \"" + title + "\" (encrypted)");
    // #69 push the derived topic to the UI so it switches the directory view to the just-added stream
    // (else the discovered station is hidden by the public-directory filter). A QtRO slot's RETURN
    // value can't be read synchronously from QML — must be a signal.
    emit privateStreamAdded(topic);
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

    // #69 private streams: an encrypted announce arrives as a {pv,enc,n,ct} envelope. Try each private
    // stream's key (the AEAD tag identifies the right one, and fails closed for all others) — on success
    // continue with the decrypted plaintext announce. A relay with no matching key just can't read it.
    if (StationCrypto::isEnvelope(QString::fromUtf8(json))) {
        const QString env = QString::fromUtf8(json);
        QByteArray plain;
        bool decrypted = false;
        for (auto it = m_privStreams.constBegin(); it != m_privStreams.constEnd(); ++it) {
            if (StationCrypto::decryptAnnounce(it.value().key, env, it.value().seg, plain)) { decrypted = true; break; }
        }
        if (!decrypted) return;   // not for us / undecodable — drop silently
        json = plain;
    }

    const QJsonObject o = QJsonDocument::fromJson(json).object();
    const QString name = o.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) return;

    Station s;
    s.name      = name;
    s.host      = o.value(QStringLiteral("hostLabel")).toString();
    s.streamUrl = o.value(QStringLiteral("streamUrl")).toString();
    // Detect onion from the stream URL (the announce doesn't carry a privacy field) so the UI shows
    // "IP hidden by Tor" instead of the "anonymous" host label. Falls back to any announced privacy.
    s.privacy   = isOnionUrl(s.streamUrl) ? QStringLiteral("onion") : o.value(QStringLiteral("privacy")).toString();
    s.topic     = o.value(QStringLiteral("announceTopic")).toString();
    // #40 now-playing: attacker-controllable announce data — cap length + strip control chars before render
    s.nowPlaying = o.value(QStringLiteral("nowPlaying")).toString()
                     .remove(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]"))).left(120);
    s.description = o.value(QStringLiteral("description")).toString()   // fallback when no now-playing
                     .remove(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]"))).left(200);
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
        s.keySource   = signedObj.value(QStringLiteral("keySource")).toString();  // #4 trusted (inside the verified bytes)
        s.verified    = true;
        // #14 keep a pinned station's last-known name/topic fresh (so it shows the right name while offline).
        if (m_pinnedMeta.contains(pubkey)) {
            QJsonObject m = m_pinnedMeta.value(pubkey);
            m["name"] = s.name; m["topic"] = s.topic; m["streamUrl"] = s.streamUrl;
            m["privacy"] = s.privacy; m["fingerprint"] = s.fingerprint;
            m_pinnedMeta.insert(pubkey, m);
            savePins();
        }
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
        o["description"] = s.description; // shown when now-playing is empty
        o["verified"]   = s.verified;     // #13 identity
        o["pubkey"]     = s.pubkey;
        o["fingerprint"] = s.fingerprint;
        o["keySource"]  = s.keySource;    // #4 keycard vs autogen (display)
        o["uptimeS"]   = (double)((now - s.lastSeenMs) / 1000);
        arr.append(o);
    }
    setStationsJson(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    publishPinned();
}

// #14 pin a verified station by its pubkey. Only verified (signed) stations have an identity to pin to.
QString ReceiverUiBackend::pinStation(QString pubkey)
{
    pubkey = pubkey.trimmed();
    if (pubkey.isEmpty()) return QStringLiteral("only verified stations can be pinned");
    QJsonObject meta{{"pubkey", pubkey}, {"fingerprint", StationIdentity::fingerprint(pubkey)}};
    for (const Station& s : m_stations)
        if (s.pubkey == pubkey) {
            meta["name"] = s.name; meta["topic"] = s.topic; meta["streamUrl"] = s.streamUrl; meta["privacy"] = s.privacy;
            break;
        }
    m_pinnedMeta.insert(pubkey, meta);
    savePins();
    publishPinned();
    log("pinned station");
    return QString();
}

QString ReceiverUiBackend::unpinStation(QString pubkey)
{
    m_pinnedMeta.remove(pubkey.trimmed());
    savePins();
    publishPinned();
    return QString();
}

// online = a live station currently carries this pubkey (m_stations is already TTL-pruned). Display fields
// come from the live announce when online, else the last-known persisted meta (shows the remembered name).
void ReceiverUiBackend::publishPinned()
{
    QJsonArray arr;
    for (auto it = m_pinnedMeta.constBegin(); it != m_pinnedMeta.constEnd(); ++it) {
        QJsonObject o = it.value();
        const Station* live = nullptr;
        for (const Station& s : m_stations)
            if (s.pubkey == it.key()) { live = &s; break; }
        o["online"] = (live != nullptr);
        o["pinned"] = true;
        if (live) {
            o["name"] = live->name; o["streamUrl"] = live->streamUrl; o["privacy"] = live->privacy;
            o["topic"] = live->topic; o["nowPlaying"] = live->nowPlaying; o["fingerprint"] = live->fingerprint;
            o["description"] = live->description;
        }
        arr.append(o);
    }
    setPinnedJson(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void ReceiverUiBackend::loadPins()
{
    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    const QJsonArray arr = QJsonDocument::fromJson(s.value(QStringLiteral("pinnedMeta")).toByteArray()).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        const QString pk = o.value(QStringLiteral("pubkey")).toString();
        if (!pk.isEmpty()) m_pinnedMeta.insert(pk, o);
    }
}

void ReceiverUiBackend::savePins()
{
    QJsonArray arr;
    for (const QJsonObject& o : m_pinnedMeta) arr.append(o);
    QSettings s{QLatin1String(kSettingsOrg), QLatin1String(kSettingsApp)};
    s.setValue(QStringLiteral("pinnedMeta"), QJsonDocument(arr).toJson(QJsonDocument::Compact));
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

    QString program; QStringList args;
    QProcessEnvironment env = cleanSpawnEnv();
    // #75 A bundled ffplay is nix-built (nix glibc): its SDL2 must reach the host audio server WITHOUT
    // loading glibc-mismatched system audio libs (that segfaults). We ship libpulse from the SAME nixpkgs
    // and force SDL's pulse backend, which talks to the host pulseaudio / pipewire-pulse over the socket —
    // protocol-stable, no lib-mixing. System ffplay (has all backends) is left to auto-detect.
    // #76 priority list: pulse (host pulseaudio / pipewire-pulse — covers modern desktops) → alsa
    // (system libasound; the packaged ffplay uses the system loader/glibc so no lib-mix). SDL2 falls
    // through the list, so a pure-ALSA host still gets audio. NOT bare "pulse" — that fails with no daemon.
    if (!m_moduleDir.isEmpty() && ffplay.startsWith(m_moduleDir + QStringLiteral("/bin/")))
        env.insert(QStringLiteral("SDL_AUDIODRIVER"), QStringLiteral("pulse,alsa"));
    if (onion) {
#ifdef __APPLE__
        // macOS: torsocks (LD_PRELOAD) is unusable under SIP. Route .onion playback through a local privoxy
        // HTTP proxy (forward-socks5t → listener tor SOCKS); ffplay honors -http_proxy for the HLS manifest
        // AND every child segment, and privoxy forwards ALL requests via tor (no IP/DNS leak). #7
        const QString perr = ensurePlaybackProxy();
        if (!perr.isEmpty()) { qWarning() << "receiver_ui: playback proxy failed:" << perr; return perr; }
        const QString px = QStringLiteral("http://127.0.0.1:") + QString::number(m_playProxyPort);
        ffargs << "-http_proxy" << px << m_playingUrl;
        env.insert("http_proxy", px);
        program = ffplay; args = ffargs;
#else
        // Linux: ffmpeg has no native SOCKS → route .onion playback through torsocks (LD_PRELOAD → tor SOCKS).
        ffargs << m_playingUrl;
        env.insert("TORSOCKS_TOR_ADDRESS", "127.0.0.1");            // lock torsocks onto OUR tor (Senty ISSUE-4)
        env.insert("TORSOCKS_TOR_PORT", QString::number(m_listenSocksPort));
        env.insert("TORSOCKS_ISOLATE_PID", "1");
        // #75 zero-install: when we have a bundled libtorsocks (shipped next to the bundled ffplay), LD_PRELOAD
        // it into ffplay DIRECTLY rather than shelling through the `torsocks` wrapper — nix's wrapper hardcodes
        // its own store prefix, so it can't be relocated into the module. Same effect (intercept connect→SOCKS),
        // and the .so is from the same nixpkgs as ffplay so their glibc matches (a system libtorsocks would not).
        const QString bundledTs = (!m_moduleDir.isEmpty() && ffplay.startsWith(m_moduleDir + QStringLiteral("/bin/")))
            ? m_moduleDir + QStringLiteral("/bin/libtorsocks.so") : QString();
        if (!bundledTs.isEmpty() && QFileInfo::exists(bundledTs)) {
            env.insert("LD_PRELOAD", bundledTs);
            program = ffplay; args = ffargs;
        } else {
            program = resolveBin(QStringLiteral("torsocks"), "RECEIVER_TORSOCKS_BIN");   // fall back to system torsocks
            args = QStringList() << ffplay << ffargs;
        }
#endif
    } else {
        ffargs << m_playingUrl;
        program = ffplay; args = ffargs;
    }

    m_player = new QProcess(this);
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
    killPlaybackProxy();                            // #7 tear down the macOS privoxy bridge (no-op on Linux)
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
    return err.isEmpty() ? QStringLiteral("tor_listen_failed — free stuck ports via Settings ▸ Kill Tor Listeners") : err;
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

// #7 macOS .onion playback bridge: ffmpeg has no native SOCKS and torsocks (LD_PRELOAD) is unusable under
// SIP, so front the listener tor SOCKS with a local privoxy HTTP proxy and point ffplay at it via
// -http_proxy. On Linux this is a no-op (torsocks is used instead). Returns "" on success, else error code.
#ifdef __APPLE__
QString ReceiverUiBackend::ensurePlaybackProxy()
{
    if (m_playProxy && m_playProxy->state() == QProcess::Running) return QString();
    const int socks = m_listenSocksPort;
    if (socks <= 0) return QStringLiteral("no_socks");
    m_playProxyDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                     + "/receiver_ui/privoxy-" + QString::number(QCoreApplication::applicationPid());
    auto fail = [&](const QString& code) {
        QDir(m_playProxyDir).removeRecursively(); m_playProxyDir.clear(); m_playProxyPort = 0; return code;
    };
    if (!QDir().mkpath(m_playProxyDir)) return fail(QStringLiteral("proxy_dir_failed"));
    QFile::setPermissions(m_playProxyDir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    // Loopback HTTP port for privoxy; offset off the SOCKS port to avoid colliding with it.
    m_playProxyPort = socks + 1000;
    const QString cfgPath = m_playProxyDir + "/privoxy.cfg";
    QFile cfg(cfgPath);
    if (!cfg.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail(QStringLiteral("proxy_cfg_failed"));
    {
        // Pure forwarder — no actionsfile/filterfile (no content filtering of the mpegts). forward-socks5t
        // (trailing t) = REMOTE DNS so the .onion resolves at the tor exit; "/ ... ." forwards ALL requests
        // through tor (no clearnet/DNS leak — preserves the torsocks no-leak property).
        QTextStream s(&cfg);
        s << "listen-address 127.0.0.1:" << m_playProxyPort << "\n"
          << "forward-socks5t / 127.0.0.1:" << socks << " .\n";
    }
    cfg.close();
    const QString bin = resolveBin(QStringLiteral("privoxy"), "RECEIVER_PRIVOXY_BIN");
    m_playProxy = new QProcess(this);
    m_playProxy->setProcessChannelMode(QProcess::MergedChannels);
    m_playProxy->setProcessEnvironment(cleanSpawnEnv());
    m_playProxy->start(bin, QStringList() << "--no-daemon" << cfgPath);
    if (!m_playProxy->waitForStarted(5000)) {
        const bool notFound = m_playProxy->error() == QProcess::FailedToStart;
        qWarning() << "receiver_ui: privoxy failed to start:" << m_playProxy->errorString();
        m_playProxy->deleteLater(); m_playProxy = nullptr;
        return fail(notFound ? QStringLiteral("privoxy_not_found (install privoxy)") : QStringLiteral("privoxy_start_failed"));
    }
    if (m_playProxy->waitForFinished(400)) {   // immediate exit ⇒ bad config or port already in use
        qWarning() << "receiver_ui: privoxy exited immediately:" << m_playProxy->readAll();
        m_playProxy->deleteLater(); m_playProxy = nullptr;
        return fail(QStringLiteral("privoxy_port_in_use"));
    }
    return QString();
}

void ReceiverUiBackend::killPlaybackProxy()
{
    if (m_playProxy) {
        m_playProxy->terminate();
        if (!m_playProxy->waitForFinished(3000)) m_playProxy->kill();
        m_playProxy->deleteLater();
        m_playProxy = nullptr;
    }
    if (!m_playProxyDir.isEmpty()) { QDir(m_playProxyDir).removeRecursively(); m_playProxyDir.clear(); }
    m_playProxyPort = 0;
}
#else
QString ReceiverUiBackend::ensurePlaybackProxy() { return QString(); }
void    ReceiverUiBackend::killPlaybackProxy() {}
#endif

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
        qWarning() << "receiver_ui: tor exited immediately (port in use — try Settings ▸ Kill Tor Listeners):" << m_torListen->readAll();
        m_torListen->deleteLater(); m_torListen = nullptr;
        return fail(QStringLiteral("tor_port_in_use — free stuck ports via Settings ▸ Kill Tor Listeners"));
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

// #52 Reap leaked receiver_ui Tor listeners. Every Play spawns a `torlisten-<hex>` tor holding a SOCKS port
// (9250–9253); a crash or an abandoned session leaves it running, so the next Play hits "port in use" /
// can't connect. This kills THIS instance's listener + any ORPHANED torlisten-* daemon (from dead or other
// instances), clears their /tmp dirs, and respawns a fresh one so the next Play works immediately.
QString ReceiverUiBackend::killTorListeners()
{
    killTorListen();   // graceful stop of our own current listener first

    int reaped = 0;
    const QByteArray marker = QByteArrayLiteral("receiver_ui/torlisten-");
#if defined(__linux__)
    // Linux: scan /proc cmdlines directly (fast, no subprocess).
    QDir proc(QStringLiteral("/proc"));
    const auto pids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& pid : pids) {
        bool isNum = false;
        const int p = pid.toInt(&isNum);
        if (!isNum) continue;
        QFile f(QStringLiteral("/proc/%1/cmdline").arg(pid));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray cmd = f.readAll();   // NUL-separated argv
        f.close();
        if (!cmd.contains(marker)) continue;  // not one of our torlisten daemons
        // Require argv[0] to be the tor binary — NEVER SIGKILL a shell/editor/agent that merely
        // references the path in its command line (the classic pkill -f self-match footgun). Only real tor.
        const QByteArray argv0 = cmd.left(cmd.indexOf('\0'));
        if (argv0 != "tor" && !argv0.endsWith("/tor")) continue;
        ::kill(p, SIGKILL);                   // stuck listener → SIGKILL frees the SOCKS port immediately
        ++reaped;
    }
#elif defined(__APPLE__)
    // macOS has no /proc → enumerate via `pgrep -f`, then confirm each pid is actually `tor` via `ps -o comm=`
    // (the comm check is the argv0==tor guard's equivalent — never kill a shell/agent matching the marker).
    // NOTE: untested on-device (mac build is gated on MACOS-BUILD-PROTOCOL); verify with a physical mac run.
    QProcess pg;
    pg.start(QStringLiteral("pgrep"), {QStringLiteral("-f"), QString::fromUtf8(marker)});
    pg.waitForFinished(3000);
    for (const QByteArray& line : pg.readAllStandardOutput().split('\n')) {
        bool ok = false;
        const int p = line.trimmed().toInt(&ok);
        if (!ok) continue;
        QProcess cm;
        cm.start(QStringLiteral("ps"), {QStringLiteral("-p"), QString::number(p), QStringLiteral("-o"), QStringLiteral("comm=")});
        cm.waitForFinished(2000);
        if (!cm.readAllStandardOutput().trimmed().endsWith("tor")) continue;   // only real tor procs
        ::kill(p, SIGKILL);
        ++reaped;
    }
#endif

    int dirs = 0;   // sweep the leaked torlisten-* dirs
    QDir base(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/receiver_ui"));
    for (const QString& d : base.entryList(QStringList() << QStringLiteral("torlisten-*"),
                                           QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QDir(base.filePath(d)).removeRecursively()) ++dirs;
    }

    ensureTorListen();   // fresh listener on a now-free port
    const QString msg = QStringLiteral("killed %1 tor listener(s), cleared %2 dir(s)").arg(reaped).arg(dirs);
    log(msg);
    return msg;
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
