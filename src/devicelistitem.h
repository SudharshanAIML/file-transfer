// devicelistitem.h — custom widget for a single discovered peer in the device list
#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include "transfersession.h"
#include "devicediscovery.h"

class DeviceListItem : public QWidget {
    Q_OBJECT
public:
    explicit DeviceListItem(const PeerInfo &peer, QWidget *parent = nullptr);

    void updatePeer(const PeerInfo &peer);
    PeerInfo peer() const { return m_peer; }

signals:
    void sendClicked(const PeerInfo &peer);

private:
    PeerInfo  m_peer;
    QLabel   *m_nameLabel{nullptr};
    QLabel   *m_addrLabel{nullptr};
    QLabel   *m_statusDot{nullptr};
    QPushButton *m_sendBtn{nullptr};
};
