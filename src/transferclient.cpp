// transferclient.cpp
#include "transferclient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDataStream>
#include <QFileInfo>

// ── Framing ────────────────────────────────────────────────────────────────────
QByteArray TransferClient::buildFrame(MsgType type, const QByteArray &payload)
{
    QByteArray frame;
    QDataStream ds(&frame, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::BigEndian);
    ds << static_cast<quint8>(type);
    ds << static_cast<quint32>(payload.size());
    frame.append(payload);
    return frame;
}

// ── TransferClient ─────────────────────────────────────────────────────────────
TransferClient::TransferClient(QObject *parent) : QObject(parent) {}

TransferClient::~TransferClient()
{
    cancel();
}

void TransferClient::sendFiles(const QHostAddress &host, quint16 port,
                               const QString &localDeviceName,
                               const QList<FileEntry> &files)
{
    m_deviceName = localDeviceName;
    m_files      = files;
    m_totalBytes = 0;
    for (const auto &f : files) m_totalBytes += f.size;

    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected,    this, &TransferClient::onConnected);
    connect(m_socket, &QTcpSocket::readyRead,    this, &TransferClient::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &TransferClient::onDisconnected);
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this, &TransferClient::onError);

    m_state = State::Connecting;
    m_socket->connectToHost(host, port);
}

void TransferClient::cancel()
{
    m_cancelled = true;
    if (m_pumpTimer) { m_pumpTimer->stop(); }
    if (m_inFile.isOpen()) m_inFile.close();
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->disconnectFromHost();
}

void TransferClient::onConnected()
{
    sendTransferRequest();
    m_state = State::WaitingForAcceptance;
}

void TransferClient::sendTransferRequest()
{
    QJsonArray filesArray;
    for (const auto &f : m_files) {
        QJsonObject fo;
        fo["name"]    = f.displayName;
        fo["relPath"] = f.relativePath;
        fo["size"]    = f.size;
        filesArray.append(fo);
    }

    QJsonObject obj;
    obj["deviceName"] = m_deviceName;
    obj["files"]      = filesArray;
    obj["totalBytes"] = m_totalBytes;

    QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    m_socket->write(buildFrame(MsgType::TransferRequest, payload));
}

void TransferClient::onReadyRead()
{
    m_recvBuffer.append(m_socket->readAll());

    while (m_recvBuffer.size() >= 5) {
        MsgType type   = static_cast<MsgType>(static_cast<quint8>(m_recvBuffer[0]));
        quint32 length = 0;
        {
            QDataStream ds(m_recvBuffer.mid(1, 4));
            ds.setByteOrder(QDataStream::BigEndian);
            ds >> length;
        }
        if (m_recvBuffer.size() < 5 + static_cast<int>(length)) break;
        m_recvBuffer.remove(0, 5 + int(length));

        if (m_state == State::WaitingForAcceptance) {
            if (type == MsgType::TransferAccepted) {
                m_state = State::Sending;
                startNextFile();
            } else if (type == MsgType::TransferRejected) {
                m_state = State::Done;
                emit transferRejected();
            }
        } else if (m_state == State::Sending) {
            if (type == MsgType::FileAck) {
                // Previous file acknowledged — move on
                if (m_currentFileIdx + 1 < m_files.size()) {
                    startNextFile();
                } else {
                    // All files sent
                    m_socket->write(buildFrame(MsgType::TransferComplete));
                    m_state = State::Done;
                    emit transferFinished(true, "All files sent successfully.");
                }
            }
        }
    }
}

void TransferClient::startNextFile()
{
    ++m_currentFileIdx;
    if (m_currentFileIdx >= m_files.size()) {
        m_socket->write(buildFrame(MsgType::TransferComplete));
        m_state = State::Done;
        emit transferFinished(true, "All files sent successfully.");
        return;
    }

    const FileEntry &entry = m_files[m_currentFileIdx];
    m_inFile.setFileName(entry.absolutePath);
    if (!m_inFile.open(QIODevice::ReadOnly)) {
        emit transferFinished(false, QString("Cannot open file: %1").arg(entry.absolutePath));
        m_state = State::Done;
        return;
    }

    sendFileHeader(m_currentFileIdx);
    m_currentFileSent = 0;

    // Start async pump
    if (!m_pumpTimer) {
        m_pumpTimer = new QTimer(this);
        connect(m_pumpTimer, &QTimer::timeout, this, &TransferClient::pumpData);
    }
    m_pumpTimer->start(0); // fire as fast as possible via event loop
}

void TransferClient::sendFileHeader(int idx)
{
    const FileEntry &entry = m_files[idx];
    QJsonObject obj;
    obj["name"]    = entry.displayName;
    obj["relPath"] = entry.relativePath;
    obj["size"]    = entry.size;

    QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    m_socket->write(buildFrame(MsgType::FileHeader, payload));
}

void TransferClient::pumpData()
{
    if (m_cancelled || m_state != State::Sending) {
        m_pumpTimer->stop();
        return;
    }

    // Don't flood the socket buffer — only write if the output buffer is small
    if (m_socket->bytesToWrite() > CHUNK_SIZE * 2) return;

    QByteArray chunk = m_inFile.read(CHUNK_SIZE);
    if (chunk.isEmpty()) {
        // File fully read; wait for FileAck from receiver before next file
        m_pumpTimer->stop();
        m_inFile.close();
        return;
    }

    m_socket->write(buildFrame(MsgType::FileData, chunk));
    m_currentFileSent += chunk.size();
    m_totalSent       += chunk.size();

    emit progressUpdate(m_totalSent, m_totalBytes,
                        m_files[m_currentFileIdx].displayName);
}

void TransferClient::onError(QAbstractSocket::SocketError)
{
    if (m_state == State::Done) return;
    m_state = State::Done;
    emit transferFinished(false, m_socket->errorString());
}

void TransferClient::onDisconnected()
{
    if (m_state != State::Done) {
        m_state = State::Done;
        emit transferFinished(false, "Connection closed unexpectedly.");
    }
}
