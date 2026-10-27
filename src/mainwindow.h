// mainwindow.h
#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QMap>

#include "devicediscovery.h"
#include "transferserver.h"
#include "transferclient.h"
#include "devicelistitem.h"
#include "transferwidget.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // Discovery
    void onPeerDiscovered(const PeerInfo &peer);
    void onPeerLost(const QString &name);

    // Outgoing
    void onSendToDevice(const PeerInfo &peer);

    // Incoming
    void onIncomingRequest(IncomingConnection *conn, const IncomingTransfer &info);
    void onIncomingProgress(IncomingConnection *conn, qint64 recv, qint64 total,
                            const QString &file);
    void onIncomingFinished(IncomingConnection *conn, bool ok, const QString &msg);

    // Settings
    void onDeviceNameChanged();

private:
    void setupUi();
    void setupTray();
    QString localDeviceName() const;
    void addTransferWidget(void *key, TransferWidget *w);
    void removeTransferWidget(void *key);

    // ── Sidebar navigation ────────────────────────────────────────────────────
    QStackedWidget *m_pages{nullptr};
    QPushButton    *m_navDevices{nullptr};
    QPushButton    *m_navTransfers{nullptr};
    QPushButton    *m_navSettings{nullptr};

    // ── Devices page ──────────────────────────────────────────────────────────
    QWidget      *m_devicesPage{nullptr};
    QVBoxLayout  *m_deviceListLayout{nullptr};
    QLabel       *m_noDevicesLabel{nullptr};
    QMap<QString, DeviceListItem*> m_deviceItems; // key = deviceName

    // ── Transfers page ────────────────────────────────────────────────────────
    QWidget      *m_transfersPage{nullptr};
    QVBoxLayout  *m_transfersLayout{nullptr};
    QLabel       *m_noTransfersLabel{nullptr};
    QMap<void*,  TransferWidget*> m_transferWidgets;

    // ── Settings page ─────────────────────────────────────────────────────────
    QWidget    *m_settingsPage{nullptr};
    QLineEdit  *m_deviceNameEdit{nullptr};
    QLabel     *m_listenAddrLabel{nullptr};

    // ── Core components ───────────────────────────────────────────────────────
    DeviceDiscovery  *m_discovery{nullptr};
    TransferServer   *m_server{nullptr};

    // Active outgoing transfers: key = TransferClient*
    QMap<TransferClient*, TransferWidget*> m_outgoing;

    QSystemTrayIcon *m_tray{nullptr};
};
