// devicediscovery.cpp
#include "devicediscovery.h"
#include "protocol.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QCoreApplication>

DeviceDiscovery::DeviceDiscovery(const QString &deviceName, QObject *parent)
    : QObject(parent), m_deviceName(deviceName)
{
}

DeviceDiscovery::~DeviceDiscovery()
{
    stop();
}

void DeviceDiscovery::start()
{
    m_socket = new QUdpSocket(this);
    m_socket->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 1);

    // Bind to the UDP port on all interfaces, allow re-use so multiple
    // instances on the same machine (for testing) can share the port.
    if (!m_socket->bind(QHostAddress::AnyIPv4, LANSHARE_UDP_PORT,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning("DeviceDiscovery: failed to bind UDP port %d: %s",
                 LANSHARE_UDP_PORT, qPrintable(m_socket->errorString()));
    }

    connect(m_socket, &QUdpSocket::readyRead, this, &DeviceDiscovery::onReadyRead);

    m_broadcastTimer = new QTimer(this);
    connect(m_broadcastTimer, &QTimer::timeout, this, &DeviceDiscovery::onBroadcastTimer);
    m_broadcastTimer->start(DISCOVERY_INTERVAL_MS);

    m_pruneTimer = new QTimer(this);
    connect(m_pruneTimer, &QTimer::timeout, this, &DeviceDiscovery::onPruneTimer);
    m_pruneTimer->start(2000);

    // Announce immediately
    sendAnnouncement();
}

void DeviceDiscovery::stop()
{
    if (m_broadcastTimer) { m_broadcastTimer->stop(); }
    if (m_pruneTimer)     { m_pruneTimer->stop(); }
    if (m_socket)         { m_socket->close(); }
}

QList<PeerInfo> DeviceDiscovery::peers() const
{
    return m_peers.values();
}

void DeviceDiscovery::onBroadcastTimer()
{
    sendAnnouncement();
}

void DeviceDiscovery::sendAnnouncement()
{
    if (!m_socket) return;

    QJsonObject obj;
    obj["type"]       = "announce";
    obj["deviceName"] = m_deviceName;
    obj["tcpPort"]    = LANSHARE_PORT;
    obj["version"]    = "1";

    QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    // Broadcast on all network interfaces
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp))       continue;
        if (!(iface.flags() & QNetworkInterface::CanBroadcast)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack)    continue;

        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            QHostAddress bcast = entry.broadcast();
            if (bcast.isNull()) continue;
            m_socket->writeDatagram(data, bcast, LANSHARE_UDP_PORT);
        }
    }
}

void DeviceDiscovery::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray data;
        QHostAddress sender;
        quint16 senderPort;
        data.resize(int(m_socket->pendingDatagramSize()));
        m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);
        processDatagram(data, sender);
    }
}

void DeviceDiscovery::processDatagram(const QByteArray &data, const QHostAddress &sender)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;

    QJsonObject obj = doc.object();
    if (obj.value("type").toString() != "announce") return;

    QString peerName = obj.value("deviceName").toString();
    if (peerName.isEmpty() || peerName == m_deviceName) return;

    quint16 tcpPort = static_cast<quint16>(obj.value("tcpPort").toInt(LANSHARE_PORT));

    bool isNew = !m_peers.contains(peerName);

    PeerInfo &peer   = m_peers[peerName];
    peer.deviceName  = peerName;
    peer.address     = sender.toIPv4Address() ? sender : QHostAddress(sender.toIPv4Address());
    // Normalize IPv4-mapped IPv6 addresses
    if (sender.protocol() == QAbstractSocket::IPv6Protocol) {
        bool ok;
        quint32 v4 = sender.toIPv4Address(&ok);
        if (ok) peer.address = QHostAddress(v4);
        else    peer.address = sender;
    } else {
        peer.address = sender;
    }
    peer.tcpPort     = tcpPort;
    peer.lastSeen    = QDateTime::currentDateTime();

    if (isNew) {
        emit peerDiscovered(peer);
    }
}

void DeviceDiscovery::onPruneTimer()
{
    const QDateTime now = QDateTime::currentDateTime();
    QStringList toRemove;

    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        if (it->lastSeen.msecsTo(now) > PEER_TIMEOUT_MS) {
            toRemove << it.key();
        }
    }

    for (const QString &name : toRemove) {
        m_peers.remove(name);
        emit peerLost(name);
    }
}
