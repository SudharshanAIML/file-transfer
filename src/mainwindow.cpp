// mainwindow.cpp
#include "mainwindow.h"
#include "filebrowserdialog.h"
#include "protocol.h"
#include <QApplication>
#include <QCloseEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QScrollArea>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QSettings>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpacerItem>
#include <QListWidget>
#include <QStackedWidget>
#include <QTimer>
#include <QFont>
#include <QFontDatabase>

// ── Style sheet ────────────────────────────────────────────────────────────────
static const char *APP_STYLE = R"(
    QMainWindow, QWidget#centralWidget {
        background: #13131f;
    }
    /* Sidebar */
    QWidget#sidebar {
        background: #0d0d1a;
        border-right: 1px solid #1e1e38;
        min-width: 200px;
        max-width: 200px;
    }
    QPushButton.navBtn {
        background: transparent;
        color: #7070a0;
        border: none;
        border-radius: 8px;
        padding: 10px 18px;
        font-size: 14px;
        text-align: left;
    }
    QPushButton.navBtn:hover {
        background: #1a1a30;
        color: #c0c0ff;
    }
    QPushButton.navBtn:checked {
        background: #252548;
        color: #a0aaff;
        font-weight: bold;
    }
    /* Content area */
    QWidget#contentArea {
        background: #13131f;
    }
    QScrollArea {
        background: transparent;
        border: none;
    }
    QScrollArea > QWidget > QWidget {
        background: transparent;
    }
    QScrollBar:vertical {
        background: #0d0d1a;
        width: 6px;
        border-radius: 3px;
    }
    QScrollBar::handle:vertical {
        background: #2a2a5a;
        border-radius: 3px;
        min-height: 20px;
    }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    /* Generic labels */
    QLabel {
        color: #c0c0e0;
        font-size: 13px;
    }
    QLineEdit {
        background: #1a1a30;
        color: #e0e0ff;
        border: 1px solid #2a2a5a;
        border-radius: 6px;
        padding: 6px 10px;
        font-size: 13px;
    }
    QLineEdit:focus {
        border-color: #667eea;
    }
    QGroupBox {
        color: #7070a0;
        border: 1px solid #2a2a4a;
        border-radius: 10px;
        margin-top: 12px;
        font-size: 12px;
        padding: 8px;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        left: 12px;
        padding: 0 4px;
        color: #6060a0;
    }
)";

// ── Helpers ────────────────────────────────────────────────────────────────────
static QPushButton *makeNavButton(const QString &text)
{
    auto *btn = new QPushButton(text);
    btn->setCheckable(true);
    btn->setProperty("class", "navBtn");
    btn->setStyleSheet(R"(
        QPushButton {
            background: transparent;
            color: #7070a0;
            border: none;
            border-radius: 8px;
            padding: 10px 18px;
            font-size: 14px;
            text-align: left;
        }
        QPushButton:hover { background: #1a1a30; color: #c0c0ff; }
        QPushButton:checked { background: #252548; color: #a0aaff; font-weight: bold; }
    )");
    return btn;
}

static QString localIPs()
{
    QStringList ips;
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                ips << entry.ip().toString();
        }
    }
    return ips.isEmpty() ? "Not connected" : ips.join(", ");
}

// ── MainWindow ─────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("LANShare");
    setMinimumSize(980, 650);
    resize(1100, 700);
    setStyleSheet(APP_STYLE);

    setupUi();
    setupTray();

    // Device discovery
    m_discovery = new DeviceDiscovery(localDeviceName(), this);
    connect(m_discovery, &DeviceDiscovery::peerDiscovered,
            this, &MainWindow::onPeerDiscovered);
    connect(m_discovery, &DeviceDiscovery::peerLost,
            this, &MainWindow::onPeerLost);
    m_discovery->start();

    // TCP server
    m_server = new TransferServer(this);
    connect(m_server, &TransferServer::incomingRequest,
            this, &MainWindow::onIncomingRequest);
    connect(m_server, &TransferServer::connectionProgress,
            this, &MainWindow::onIncomingProgress);
    connect(m_server, &TransferServer::connectionFinished,
            this, &MainWindow::onIncomingFinished);
    m_server->start(LANSHARE_PORT);

    // Update listen address label
    m_listenAddrLabel->setText(localIPs() +
                               QString(":%1").arg(LANSHARE_PORT));
}

MainWindow::~MainWindow() = default;

// ── UI construction ────────────────────────────────────────────────────────────
void MainWindow::setupUi()
{
    auto *central = new QWidget;
    central->setObjectName("centralWidget");
    setCentralWidget(central);

    auto *rootHBox = new QHBoxLayout(central);
    rootHBox->setSpacing(0);
    rootHBox->setContentsMargins(0, 0, 0, 0);

    // ── Sidebar ───────────────────────────────────────────────────────────────
    auto *sidebar = new QWidget;
    sidebar->setObjectName("sidebar");

    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(10, 20, 10, 20);
    sideLayout->setSpacing(4);

    // Logo
    auto *logo = new QLabel("🔗  LANShare");
    logo->setStyleSheet("font-size: 18px; font-weight: bold; color: #a0aaff;"
                        " padding: 0 8px 16px 8px;");
    sideLayout->addWidget(logo);

    // Nav separator
    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background: #1e1e38; border: none; max-height: 1px;");
    sideLayout->addWidget(sep);
    sideLayout->addSpacing(8);

    // Nav buttons
    m_navDevices   = makeNavButton("💻  Devices");
    m_navTransfers = makeNavButton("⇄  Transfers");
    m_navSettings  = makeNavButton("⚙️  Settings");

    auto *navGroup = new QButtonGroup(this);
    navGroup->setExclusive(true);
    navGroup->addButton(m_navDevices);
    navGroup->addButton(m_navTransfers);
    navGroup->addButton(m_navSettings);

    sideLayout->addWidget(m_navDevices);
    sideLayout->addWidget(m_navTransfers);
    sideLayout->addWidget(m_navSettings);
    sideLayout->addStretch();

    // Version
    auto *verLabel = new QLabel("v1.0.0");
    verLabel->setStyleSheet("color: #303060; font-size: 11px; padding: 0 8px;");
    sideLayout->addWidget(verLabel);

    rootHBox->addWidget(sidebar);

    // ── Pages ─────────────────────────────────────────────────────────────────
    m_pages = new QStackedWidget;
    m_pages->setObjectName("contentArea");
    rootHBox->addWidget(m_pages, 1);

    // ── Devices page ──────────────────────────────────────────────────────────
    {
        m_devicesPage = new QWidget;
        auto *pageLayout = new QVBoxLayout(m_devicesPage);
        pageLayout->setContentsMargins(24, 24, 24, 24);
        pageLayout->setSpacing(16);

        auto *hdr = new QLabel("Discovered Devices");
        hdr->setStyleSheet("font-size: 22px; font-weight: bold; color: #d0d0ff;");
        pageLayout->addWidget(hdr);

        auto *sub = new QLabel("Devices on your LAN running LANShare appear here automatically.");
        sub->setStyleSheet("color: #6060a0; font-size: 12px;");
        pageLayout->addWidget(sub);

        auto *scrollArea = new QScrollArea;
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);

        auto *scrollContent = new QWidget;
        m_deviceListLayout  = new QVBoxLayout(scrollContent);
        m_deviceListLayout->setAlignment(Qt::AlignTop);
        m_deviceListLayout->setSpacing(10);
        m_deviceListLayout->setContentsMargins(0, 0, 10, 0);

        m_noDevicesLabel = new QLabel(
            "🔍  Scanning for devices…\n\n"
            "Make sure other devices have LANShare running\n"
            "and are connected to the same Wi-Fi or LAN.");
        m_noDevicesLabel->setAlignment(Qt::AlignCenter);
        m_noDevicesLabel->setStyleSheet(
            "color: #404080; font-size: 14px; padding: 60px;");
        m_deviceListLayout->addWidget(m_noDevicesLabel);

        scrollArea->setWidget(scrollContent);
        pageLayout->addWidget(scrollArea, 1);

        m_pages->addWidget(m_devicesPage);
    }

    // ── Transfers page ────────────────────────────────────────────────────────
    {
        m_transfersPage = new QWidget;
        auto *pageLayout = new QVBoxLayout(m_transfersPage);
        pageLayout->setContentsMargins(24, 24, 24, 24);
        pageLayout->setSpacing(16);

        auto *hdr = new QLabel("Active Transfers");
        hdr->setStyleSheet("font-size: 22px; font-weight: bold; color: #d0d0ff;");
        pageLayout->addWidget(hdr);

        auto *scrollArea = new QScrollArea;
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);

        auto *scrollContent  = new QWidget;
        m_transfersLayout    = new QVBoxLayout(scrollContent);
        m_transfersLayout->setAlignment(Qt::AlignTop);
        m_transfersLayout->setSpacing(10);
        m_transfersLayout->setContentsMargins(0, 0, 10, 0);

        m_noTransfersLabel = new QLabel("No active transfers.\nSend files to a device to get started.");
        m_noTransfersLabel->setAlignment(Qt::AlignCenter);
        m_noTransfersLabel->setStyleSheet(
            "color: #404080; font-size: 14px; padding: 60px;");
        m_transfersLayout->addWidget(m_noTransfersLabel);

        scrollArea->setWidget(scrollContent);
        pageLayout->addWidget(scrollArea, 1);

        m_pages->addWidget(m_transfersPage);
    }

    // ── Settings page ─────────────────────────────────────────────────────────
    {
        m_settingsPage = new QWidget;
        auto *pageLayout = new QVBoxLayout(m_settingsPage);
        pageLayout->setContentsMargins(24, 24, 24, 24);
        pageLayout->setSpacing(20);

        auto *hdr = new QLabel("Settings");
        hdr->setStyleSheet("font-size: 22px; font-weight: bold; color: #d0d0ff;");
        pageLayout->addWidget(hdr);

        // Identity group
        auto *identGroup = new QGroupBox("Identity");
        auto *identLayout = new QFormLayout(identGroup);
        identLayout->setSpacing(10);

        QSettings settings("LANShare", "LANShare");
        QString savedName = settings.value("deviceName",
                                           QHostInfo::localHostName()).toString();

        m_deviceNameEdit = new QLineEdit(savedName);
        m_deviceNameEdit->setPlaceholderText("My Laptop");
        connect(m_deviceNameEdit, &QLineEdit::editingFinished,
                this, &MainWindow::onDeviceNameChanged);

        identLayout->addRow("Device Name:", m_deviceNameEdit);

        auto *saveBtn = new QPushButton("Save Name");
        saveBtn->setStyleSheet(R"(
            QPushButton {
                background: qlineargradient(x1:0,y1:0,x2:1,y2:0,
                             stop:0 #667eea, stop:1 #764ba2);
                color: white; border: none; border-radius: 6px;
                padding: 7px 20px; font-size: 13px; font-weight: bold;
                max-width: 140px;
            }
            QPushButton:hover { background: #7a90f5; }
        )");
        connect(saveBtn, &QPushButton::clicked, this, &MainWindow::onDeviceNameChanged);
        identLayout->addRow("", saveBtn);

        pageLayout->addWidget(identGroup);

        // Network info group
        auto *netGroup  = new QGroupBox("Network");
        auto *netLayout = new QFormLayout(netGroup);
        netLayout->setSpacing(8);

        m_listenAddrLabel = new QLabel("Detecting…");
        m_listenAddrLabel->setStyleSheet("color: #80a0ff;");
        netLayout->addRow("Listening on:", m_listenAddrLabel);

        auto *portLabel = new QLabel(QString("UDP + TCP port %1").arg(LANSHARE_PORT));
        portLabel->setStyleSheet("color: #80a0ff;");
        netLayout->addRow("Port:", portLabel);

        pageLayout->addWidget(netGroup);

        // About group
        auto *aboutGroup  = new QGroupBox("About");
        auto *aboutLayout = new QVBoxLayout(aboutGroup);
        auto *aboutText   = new QLabel(
            "LANShare v1.0.0\n"
            "Fast peer-to-peer file transfer over your local network.\n\n"
            "Built with C++20 · Qt · No cloud · No accounts required.");
        aboutText->setStyleSheet("color: #6060a0; font-size: 12px;");
        aboutText->setWordWrap(true);
        aboutLayout->addWidget(aboutText);
        pageLayout->addWidget(aboutGroup);

        pageLayout->addStretch();
        m_pages->addWidget(m_settingsPage);
    }

    // ── Nav wiring ────────────────────────────────────────────────────────────
    connect(m_navDevices, &QPushButton::clicked, this, [this]() {
        m_pages->setCurrentWidget(m_devicesPage);
    });
    connect(m_navTransfers, &QPushButton::clicked, this, [this]() {
        m_pages->setCurrentWidget(m_transfersPage);
    });
    connect(m_navSettings, &QPushButton::clicked, this, [this]() {
        m_pages->setCurrentWidget(m_settingsPage);
    });

    m_navDevices->setChecked(true);
    m_pages->setCurrentWidget(m_devicesPage);
}

void MainWindow::setupTray()
{
    m_tray = new QSystemTrayIcon(QIcon::fromTheme("network-transmit-receive"), this);
    m_tray->setToolTip("LANShare — running");
    m_tray->show();

    connect(m_tray, &QSystemTrayIcon::activated,
            this, [this](QSystemTrayIcon::ActivationReason r) {
                if (r == QSystemTrayIcon::Trigger) {
                    show(); raise(); activateWindow();
                }
            });
}

// ── Helpers ────────────────────────────────────────────────────────────────────
QString MainWindow::localDeviceName() const
{
    QSettings settings("LANShare", "LANShare");
    return settings.value("deviceName", QHostInfo::localHostName()).toString();
}

void MainWindow::addTransferWidget(void *key, TransferWidget *w)
{
    m_noTransfersLabel->hide();
    m_transfersLayout->insertWidget(0, w);
    m_transferWidgets[key] = w;
}

void MainWindow::removeTransferWidget(void *key)
{
    if (m_transferWidgets.contains(key)) {
        // Don't delete immediately — let user see final state for 5s
        auto *w = m_transferWidgets.take(key);
        QTimer::singleShot(5000, w, &QWidget::deleteLater);
    }
    if (m_transferWidgets.isEmpty())
        m_noTransfersLabel->show();
}

// ── Discovery callbacks ────────────────────────────────────────────────────────
void MainWindow::onPeerDiscovered(const PeerInfo &peer)
{
    if (m_deviceItems.contains(peer.deviceName)) {
        m_deviceItems[peer.deviceName]->updatePeer(peer);
        return;
    }

    m_noDevicesLabel->hide();
    auto *item = new DeviceListItem(peer);
    connect(item, &DeviceListItem::sendClicked,
            this, &MainWindow::onSendToDevice);

    m_deviceListLayout->insertWidget(0, item);
    m_deviceItems[peer.deviceName] = item;
}

void MainWindow::onPeerLost(const QString &name)
{
    if (auto *item = m_deviceItems.take(name)) {
        m_deviceListLayout->removeWidget(item);
        item->deleteLater();
    }
    if (m_deviceItems.isEmpty())
        m_noDevicesLabel->show();
}

// ── Outgoing transfer ─────────────────────────────────────────────────────────
void MainWindow::onSendToDevice(const PeerInfo &peer)
{
    FileBrowserDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    QList<FileEntry> files = dlg.selectedEntries();
    if (files.isEmpty()) {
        QMessageBox::information(this, "No Files", "No files were selected.");
        return;
    }

    qint64 total = 0;
    for (const auto &f : files) total += f.size;

    auto *client = new TransferClient(this);
    auto *widget = new TransferWidget(TransferDirection::Outgoing,
                                      peer.deviceName, total);

    m_outgoing[client] = widget;
    addTransferWidget(client, widget);

    // Switch to transfers page
    m_navTransfers->setChecked(true);
    m_pages->setCurrentWidget(m_transfersPage);

    connect(widget, &TransferWidget::cancelRequested, this, [this, client]() {
        client->cancel();
        removeTransferWidget(client);
        client->deleteLater();
    });

    connect(client, &TransferClient::progressUpdate,
            this, [widget](qint64 done, qint64 total, const QString &file) {
                widget->updateProgress(done, total, file);
            });

    connect(client, &TransferClient::transferFinished,
            this, [this, client, widget](bool ok, const QString &msg) {
                widget->setFinished(ok, msg);
                if (!ok)
                    m_tray->showMessage("Transfer Failed", msg,
                                        QSystemTrayIcon::Warning, 4000);
                else
                    m_tray->showMessage("Transfer Complete",
                                        "Files sent successfully.",
                                        QSystemTrayIcon::Information, 3000);
                QTimer::singleShot(6000, this, [this, client]() {
                    removeTransferWidget(client);
                    m_outgoing.remove(client);
                    client->deleteLater();
                });
            });

    connect(client, &TransferClient::transferRejected,
            this, [this, client, widget, peer]() {
                widget->setFinished(false,
                    QString("%1 declined the transfer.").arg(peer.deviceName));
                QTimer::singleShot(5000, this, [this, client]() {
                    removeTransferWidget(client);
                    m_outgoing.remove(client);
                    client->deleteLater();
                });
            });

    client->sendFiles(peer.address, peer.tcpPort, localDeviceName(), files);
}

// ── Incoming transfer ──────────────────────────────────────────────────────────
void MainWindow::onIncomingRequest(IncomingConnection *conn,
                                   const IncomingTransfer &info)
{
    // Show notification
    m_tray->showMessage("Incoming Transfer",
        QString("%1 wants to send you %2 file(s) (%3 bytes)")
            .arg(info.senderName)
            .arg(info.files.size())
            .arg(info.totalBytes),
        QSystemTrayIcon::Information, 5000);

    // Ask user
    QString detail;
    for (const auto &f : info.files)
        detail += QString("  • %1\n").arg(f.displayName);

    int ret = QMessageBox::question(this,
        "Incoming Transfer",
        QString("<b>%1</b> wants to send you <b>%2 file(s)</b>:<br><br>"
                "<pre>%3</pre>"
                "Do you want to accept?")
            .arg(info.senderName)
            .arg(info.files.size())
            .arg(detail.toHtmlEscaped()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);

    if (ret != QMessageBox::Yes) {
        conn->rejectTransfer();
        return;
    }

    QString saveDir = QFileDialog::getExistingDirectory(
        this, "Choose Save Location",
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));

    if (saveDir.isEmpty()) {
        conn->rejectTransfer();
        return;
    }

    auto *widget = new TransferWidget(TransferDirection::Incoming,
                                      info.senderName, info.totalBytes);
    addTransferWidget(conn, widget);

    m_navTransfers->setChecked(true);
    m_pages->setCurrentWidget(m_transfersPage);

    conn->acceptTransfer(saveDir);
}

void MainWindow::onIncomingProgress(IncomingConnection *conn,
                                    qint64 recv, qint64 total,
                                    const QString &file)
{
    if (auto *w = m_transferWidgets.value(conn))
        w->updateProgress(recv, total, file);
}

void MainWindow::onIncomingFinished(IncomingConnection *conn,
                                    bool ok, const QString &msg)
{
    if (auto *w = m_transferWidgets.value(conn)) {
        w->setFinished(ok, msg);
        if (ok)
            m_tray->showMessage("Transfer Complete",
                                "Files received successfully.",
                                QSystemTrayIcon::Information, 3000);
        QTimer::singleShot(6000, this, [this, conn]() {
            removeTransferWidget(conn);
        });
    }
}

// ── Settings ───────────────────────────────────────────────────────────────────
void MainWindow::onDeviceNameChanged()
{
    QString name = m_deviceNameEdit->text().trimmed();
    if (name.isEmpty()) return;

    QSettings settings("LANShare", "LANShare");
    settings.setValue("deviceName", name);
    settings.sync();

    // Restart discovery with new name
    m_discovery->stop();
    m_discovery->deleteLater();
    m_discovery = new DeviceDiscovery(name, this);
    connect(m_discovery, &DeviceDiscovery::peerDiscovered,
            this, &MainWindow::onPeerDiscovered);
    connect(m_discovery, &DeviceDiscovery::peerLost,
            this, &MainWindow::onPeerLost);
    m_discovery->start();

    QMessageBox::information(this, "Saved",
        QString("Device name changed to \"%1\".\n"
                "Restarted discovery.").arg(name));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Minimize to tray instead of quitting
    hide();
    event->ignore();
    m_tray->showMessage("LANShare",
        "Still running in the background. Click the tray icon to reopen.",
        QSystemTrayIcon::Information, 2000);
}
