#ifndef RECEIVER_UI_PLUGIN_H
#define RECEIVER_UI_PLUGIN_H

#include <QString>
#include <QVariantList>
#include <QHash>
#include <QSet>
#include <QDateTime>
#include "rep_receiver_ui_source.h"      // ReceiverUiSimpleSource (repc from src/receiver_ui.rep)
#include "logos_ui_plugin_context.h"     // LogosUiPluginContext: modules() + onContextReady()

class QTimer;
class QProcess;

// Universal ui_qml backend (#20) — bridges QML (logos.module("receiver_ui")) to the delivery_module
// via modules().delivery_module.*, replacing the legacy LogosAPI/getClient/requestObject/onEvent path.
class ReceiverUiBackend : public ReceiverUiSimpleSource,
                          public LogosUiPluginContext
{
public:
    explicit ReceiverUiBackend(QObject* parent = nullptr);
    ~ReceiverUiBackend() override;

    // .rep SLOTs (all return "" on success, else an error string)
    QString startDiscovery() override;
    QString stopDiscovery() override;
    QString addTopic(QString topic) override;
    QString play(QString streamUrl, QString stationName) override;
    QString prewarm(QString streamUrl) override;   // #26 pre-build the Tor circuit to an onion on select
    QString stopPlayback() override;
    QString setBuffer(int sec) override;
    QString setCacheHidden(bool on) override;
    QString clearCache() override;

protected:
    // Fires once modules() is wired — subscribe to delivery events + kick discovery (was initLogos).
    void onContextReady() override;

private:
    struct Station {
        QString name;
        QString host;
        QString streamUrl;
        QString privacy;
        QString topic;
        qint64  lastSeenMs = 0;
    };

    void wireDeliveryEvents();                       // subscribe to delivery events AFTER createNode (reentrancy)
    void ingestAnnounce(const QVariant& payload);   // robust base64/utf8 decode → JSON → registry
    void pruneStations();                            // drop stations past the TTL
    void publishStations();                          // rebuild stationsJson PROP from m_stations
    bool subscribeTopic(const QString& topic);
    QString directoryTopic() const;
    QString cacheDir() const;
    void log(const QString& line);

    // playback (ported from radio-basecamp's proven listener half)
    void killPlayer();
    QString startFfplay();
    void    retryOrStopPlayback(qint64 ranMs);   // ffplay self-exited: reap Tor + retry, or give up (#12)
    void    onNoAudioWatchdog();                 // ffplay running but silent → reap Tor + retry (#23)
    QString ensureTorListen();          // spawn a listener tor (SocksPort) for .onion playback
    void    killTorListen();
    bool    startTorProc(QString& dirOut, const QString& cfg, int socksPort, QString& errOut);

    bool            m_eventsWired = false;
    QTimer*         m_pruneTimer = nullptr;

    QProcess*     m_player    = nullptr;
    QProcess*     m_torListen = nullptr;
    QString       m_torListenDir;
    QString       m_playingUrl;
    int           m_listenSocksPort = 0;
    qint64        m_playStartMs = 0;    // when the current ffplay started (retry timing, #12)
    int           m_playAttempt = 0;    // consecutive auto-retries for the current station (#12)
    QTimer*       m_watchdog    = nullptr;  // #23 no-audio watchdog (single-shot per play attempt)
    bool          m_audioFlowing = false;   // #23 ffplay emitted decode stats → real audio is flowing
    int           m_reapCount   = 0;        // #23 how many times we've reaped the listener Tor this play
    QSet<QString> m_prewarmedHosts;         // #26/#28 onion hosts whose rendezvous circuit we've warmed (capped)

    QHash<QString, Station> m_stations;   // keyed by topic+name
    QSet<QString> m_subscribed;
};

#endif // RECEIVER_UI_PLUGIN_H
