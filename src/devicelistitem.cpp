// devicelistitem.cpp
#include "devicelistitem.h"
#include <QHBoxLayout>
#include <QVBoxLayout>

DeviceListItem::DeviceListItem(const PeerInfo &peer, QWidget *parent)
    : QWidget(parent), m_peer(peer)
{
    setStyleSheet(R"(
        DeviceListItem {
            background: #1e1e3a;
            border: 1px solid #2a2a5a;
            border-radius: 10px;
            padding: 6px;
        }
        DeviceListItem:hover {
            background: #252550;
            border-color: #5050a0;
        }
    )");
    setFixedHeight(68);

    auto *hbox = new QHBoxLayout(this);
    hbox->setContentsMargins(12, 6, 12, 6);
    hbox->setSpacing(12);

    // Status dot
    m_statusDot = new QLabel("●");
    m_statusDot->setStyleSheet("color: #44ff88; font-size: 14px;");
    m_statusDot->setFixedWidth(18);
    hbox->addWidget(m_statusDot);

    // Device icon
    auto *icon = new QLabel("💻");
    icon->setStyleSheet("font-size: 24px;");
    hbox->addWidget(icon);

    // Name + address
    auto *vbox = new QVBoxLayout;
    vbox->setSpacing(2);

    m_nameLabel = new QLabel(peer.deviceName);
    m_nameLabel->setStyleSheet("color: #d0d0ff; font-size: 14px; font-weight: bold;");

    m_addrLabel = new QLabel(peer.address.toString() +
                             QString(":%1").arg(peer.tcpPort));
    m_addrLabel->setStyleSheet("color: #7070a0; font-size: 11px;");

    vbox->addWidget(m_nameLabel);
    vbox->addWidget(m_addrLabel);
    hbox->addLayout(vbox, 1);

    // Send button
    m_sendBtn = new QPushButton("Send Files →");
    m_sendBtn->setStyleSheet(R"(
        QPushButton {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
                         stop:0 #667eea, stop:1 #764ba2);
            color: white;
            border: none;
            border-radius: 8px;
            padding: 8px 18px;
            font-size: 13px;
            font-weight: bold;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
                         stop:0 #7a90f5, stop:1 #9060c0);
        }
        QPushButton:pressed { background: #5050a0; }
    )");
    connect(m_sendBtn, &QPushButton::clicked,
            this, [this]() { emit sendClicked(m_peer); });
    hbox->addWidget(m_sendBtn);
}

void DeviceListItem::updatePeer(const PeerInfo &peer)
{
    m_peer = peer;
    m_nameLabel->setText(peer.deviceName);
    m_addrLabel->setText(peer.address.toString() +
                         QString(":%1").arg(peer.tcpPort));
}
