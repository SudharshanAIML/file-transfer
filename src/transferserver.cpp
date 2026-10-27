// transferserver.cpp
#include "transferserver.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>

// ── Framing helpers ────────────────────────────────────────────────────────────
// Every message on the wire:
//   [1 byte MsgType] [4 bytes BE payload length] [payload bytes]

static QByteArray buildFrame(MsgType type, const QByteArray &payload)
{
    QByteArray frame;
    QDataStream ds(&frame, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);
    ds << static_cast<quint8>(type);
    ds << static_cast<quint32>(payload.size());
    frame.append(payload);
    return frame;
}

// ── IncomingConnection ─────────────────────────────────────────────────────────
IncomingConnection::IncomingConnection(QTcpSocket *socket, QObject *parent)
    : QObject(parent), m_socket(socket)
{
    m_socket->setParent(this);
    connect(m_socket, &QTcpSocket::readyRead,   this, &IncomingConnection::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &IncomingConnection::onDisconnected);
}

IncomingConnection::~IncomingConnection()
{
    if (m_outFile.isOpen()) m_outFile.close();
}

void IncomingConnection::sendMessage(MsgType type, const QByteArray &payload)
{
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->write(buildFrame(type, payload));
}

void IncomingConnection::acceptTransfer(const QString &saveDirectory)
{
    m_saveDir = saveDirectory;
    m_state   = State::ReceivingFiles;
    sendMessage(MsgType::TransferAccepted);
}

void IncomingConnection::rejectTransfer()
{
    sendMessage(MsgType::TransferRejected);
    m_state = State::Done;
    m_socket->disconnectFromHost();
}

void IncomingConnection::onReadyRead()
{
    m_buffer.append(m_socket->readAll());

    // Need at least 5 bytes: 1 type + 4 length
    while (m_buffer.size() >= 5) {
        MsgType  type    = static_cast<MsgType>(static_cast<quint8>(m_buffer[0]));
        quint32  length  = 0;
        {
            QDataStream ds(m_buffer.mid(1, 4));
            ds.setByteOrder(QDataStream::BigEndian);
            ds >> length;
        }

        if (m_buffer.size() < 5 + static_cast<int>(length)) break; // wait for more data

        QByteArray payload = m_buffer.mid(5, int(length));
        m_buffer.remove(0, 5 + int(length));

        switch (m_state) {
        case State::WaitingForRequest:
            if (type == MsgType::TransferRequest) {
                parseRequest(payload);
            }
            break;

        case State::WaitingForAcceptance:
            // Nothing expected from sender while user decides
            break;

        case State::ReceivingFiles:
            if (type == MsgType::FileHeader) {
                parseFileHeader(payload);
            } else if (type == MsgType::FileData) {
                receiveFileData(payload);
            } else if (type == MsgType::TransferComplete) {
                emit transferFinished(this, true, "Transfer completed successfully.");
                m_state = State::Done;
            }
            break;

        case State::Done:
            break;
        }
    }
}

void IncomingConnection::parseRequest(const QByteArray &payload)
{
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) { m_socket->disconnectFromHost(); return; }

    QJsonObject obj = doc.object();
    m_transfer.senderName    = obj.value("deviceName").toString("Unknown");
    m_transfer.senderAddress = m_socket->peerAddress();
    m_transfer.totalBytes    = 0;

    QJsonArray files = obj.value("files").toArray();
    for (const auto &f : files) {
        QJsonObject fo = f.toObject();
        FileEntry entry;
        entry.displayName  = fo.value("name").toString();
        entry.relativePath = fo.value("relPath").toString(entry.displayName);
        entry.size         = fo.value("size").toVariant().toLongLong();
        m_transfer.files   << entry;
        m_transfer.totalBytes += entry.size;
    }

    m_state = State::WaitingForAcceptance;
    emit requestReceived(this, m_transfer);
}

void IncomingConnection::parseFileHeader(const QByteArray &payload)
{
    if (m_outFile.isOpen()) finishCurrentFile();

    QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) return;
    QJsonObject obj = doc.object();

    QString relPath = obj.value("relPath").toString(obj.value("name").toString());

    // Sanitize: strip any ".." components
    QStringList parts = relPath.split('/');
    parts.removeAll("..");
    parts.removeAll(".");
    relPath = parts.join('/');

    QString fullPath = QDir(m_saveDir).filePath(relPath);

    // Create intermediate directories
    QFileInfo fi(fullPath);
    QDir().mkpath(fi.absolutePath());

    m_outFile.setFileName(fullPath);
    if (!m_outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit transferFinished(this, false,
            QString("Cannot create file: %1").arg(fullPath));
        m_state = State::Done;
        return;
    }

    m_currentFileReceived = 0;
    ++m_currentFileIdx;
    emit progressUpdate(this, m_totalReceived, m_transfer.totalBytes,
                        fi.fileName());
}

void IncomingConnection::receiveFileData(const QByteArray &payload)
{
    if (!m_outFile.isOpen()) return;

    m_outFile.write(payload);
    m_currentFileReceived += payload.size();
    m_totalReceived       += payload.size();

    emit progressUpdate(this, m_totalReceived, m_transfer.totalBytes,
                        QFileInfo(m_outFile.fileName()).fileName());

    // Check if this file is done
    int idx = m_currentFileIdx;
    if (idx >= 0 && idx < m_transfer.files.size()) {
        if (m_currentFileReceived >= m_transfer.files[idx].size) {
            finishCurrentFile();
            sendMessage(MsgType::FileAck);
        }
    }
}

void IncomingConnection::finishCurrentFile()
{
    if (m_outFile.isOpen()) m_outFile.close();
}

void IncomingConnection::onDisconnected()
{
    if (m_outFile.isOpen()) m_outFile.close();
    if (m_state != State::Done) {
        emit transferFinished(this, false, "Connection lost.");
    }
}

// ── TransferServer ─────────────────────────────────────────────────────────────
TransferServer::TransferServer(QObject *parent) : QObject(parent) {}

TransferServer::~TransferServer()
{
    stop();
}

bool TransferServer::start(quint16 port)
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &TransferServer::onNewConnection);

    if (!m_server->listen(QHostAddress::AnyIPv4, port)) {
        qWarning("TransferServer: cannot listen on port %d: %s",
                 port, qPrintable(m_server->errorString()));
        return false;
    }
    qDebug("TransferServer: listening on port %d", port);
    return true;
}

void TransferServer::stop()
{
    if (m_server) m_server->close();
    for (auto *conn : m_connections) conn->deleteLater();
    m_connections.clear();
}

void TransferServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        auto *conn       = new IncomingConnection(sock, this);

        connect(conn, &IncomingConnection::requestReceived,
                this, [this, conn](IncomingConnection *, const IncomingTransfer &info) {
                    emit incomingRequest(conn, info);
                });
        connect(conn, &IncomingConnection::progressUpdate,
                this, [this, conn](IncomingConnection *, qint64 r, qint64 t, const QString &f) {
                    emit connectionProgress(conn, r, t, f);
                });
        connect(conn, &IncomingConnection::transferFinished,
                this, [this, conn](IncomingConnection *, bool ok, const QString &msg) {
                    emit connectionFinished(conn, ok, msg);
                    m_connections.removeOne(conn);
                    conn->deleteLater();
                });

        m_connections << conn;
    }
}
