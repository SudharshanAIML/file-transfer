// devicediscovery.h — UDP broadcast-based LAN peer discovery
#pragma once
#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <QHostAddress>
#include <QMap>
#include <QDateTime>

struct PeerInfo {
    QString     deviceName;
    QHostAddress address;
    quint16      tcpPort{0};
    QDateTime    lastSeen;
};

class DeviceDiscovery : public QObject {
    Q_OBJECT
public:
    explicit DeviceDiscovery(const QString &deviceName, QObject *parent = nullptr);
    ~DeviceDiscovery() override;

    void start();
    void stop();

    QList<PeerInfo> peers() const;

signals:
    void peerDiscovered(const PeerInfo &peer);
    void peerLost(const QString &deviceName);

private slots:
    void onBroadcastTimer();
    void onReadyRead();
    void onPruneTimer();

private:
    void sendAnnouncement();
    void processDatagram(const QByteArray &data, const QHostAddress &sender);

    QString      m_deviceName;
    QUdpSocket  *m_socket{nullptr};
    QTimer      *m_broadcastTimer{nullptr};
    QTimer      *m_pruneTimer{nullptr};
    QMap<QString, PeerInfo> m_peers; // keyed by deviceName
};
