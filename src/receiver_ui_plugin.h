#ifndef RECEIVER_UI_PLUGIN_H
#define RECEIVER_UI_PLUGIN_H

#include <QString>
#include <QVariantList>
#include <QHash>
#include <QSet>
#include <QDateTime>
#include "receiver_ui_interface.h"
#include "LogosViewPluginBase.h"
#include "rep_receiver_ui_source.h"

class LogosAPI;
class LogosModules;
class QTimer;
class QProcess;

class ReceiverUiPlugin : public ReceiverUiSimpleSource,
                         public ReceiverUiInterface,
                         public ReceiverUiViewPluginBase
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ReceiverUiInterface_iid FILE "metadata.json")
    Q_INTERFACES(ReceiverUiInterface)

public:
    explicit ReceiverUiPlugin(QObject* parent = nullptr);
    ~ReceiverUiPlugin() override;

    QString name()    const override { return "receiver_ui"; }
    QString version() const override { return "0.1.0"; }

    Q_INVOKABLE void initLogos(LogosAPI* api);

    // .rep SLOTs (all return "" on success, else an error string)
    QString startDiscovery() override;
    QString stopDiscovery() override;
    QString addTopic(QString topic) override;
    QString play(QString streamUrl, QString stationName) override;
    QString stopPlayback() override;
    QString setBuffer(int sec) override;
    QString setCacheHidden(bool on) override;
    QString clearCache() override;

signals:
    void eventResponse(const QString& eventName, const QVariantList& args);

private:
    struct Station {
        QString name;
        QString host;
        QString streamUrl;
        QString privacy;
        QString topic;
        qint64  lastSeenMs = 0;
    };

    void wireEvents();
    void ingestAnnounce(const QVariant& payload);   // robust base64/utf8 decode → JSON → registry
    void pruneStations();                            // drop stations past the TTL
    void publishStations();                          // rebuild stationsJson PROP from m_stations
    bool subscribeTopic(const QString& topic);
    QString directoryTopic() const;
    QString cacheDir() const;
    void log(const QString& line);

    LogosAPI*     m_logosAPI = nullptr;
    LogosModules* m_logos    = nullptr;
    QTimer*       m_pruneTimer = nullptr;
    QProcess*     m_player   = nullptr;

    QHash<QString, Station> m_stations;   // keyed by topic+name
    QSet<QString> m_subscribed;
};

#endif // RECEIVER_UI_PLUGIN_H
