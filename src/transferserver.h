// transferserver.h — TCP server that accepts incoming file transfers
#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QTimer>
#include "transfersession.h"
#include "protocol.h"

// Manages one active incoming connection
class IncomingConnection : public QObject {
    Q_OBJECT
public:
    explicit IncomingConnection(QTcpSocket *socket, QObject *parent = nullptr);
    ~IncomingConnection() override;

    void acceptTransfer(const QString &saveDirectory);
    void rejectTransfer();

signals:
    void requestReceived(IncomingConnection *conn, const IncomingTransfer &info);
    void progressUpdate(IncomingConnection *conn, qint64 bytesReceived, qint64 totalBytes,
                        const QString &currentFile);
    void transferFinished(IncomingConnection *conn, bool success, const QString &message);

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    void parseRequest(const QByteArray &payload);
    void parseFileHeader(const QByteArray &payload);
    void receiveFileData(const QByteArray &payload);
    void sendMessage(MsgType type, const QByteArray &payload = {});
    void finishCurrentFile();

    enum class State {
        WaitingForRequest,
        WaitingForAcceptance, // user deciding
        ReceivingFiles,
        Done
    };

    QTcpSocket     *m_socket;
    State           m_state{State::WaitingForRequest};
    QByteArray      m_buffer;

    IncomingTransfer m_transfer;
    QString          m_saveDir;

    int              m_currentFileIdx{-1};
    qint64           m_currentFileReceived{0};
    qint64           m_totalReceived{0};
    QFile            m_outFile;


};

// ── TCP server ─────────────────────────────────────────────────────────────────
class TransferServer : public QObject {
    Q_OBJECT
public:
    explicit TransferServer(QObject *parent = nullptr);
    ~TransferServer() override;

    bool start(quint16 port = LANSHARE_PORT);
    void stop();

signals:
    // Emitted when a peer wants to send us files; UI must call accept/reject on conn
    void incomingRequest(IncomingConnection *conn, const IncomingTransfer &info);
    void connectionProgress(IncomingConnection *conn, qint64 recv, qint64 total,
                            const QString &file);
    void connectionFinished(IncomingConnection *conn, bool ok, const QString &msg);

private slots:
    void onNewConnection();

private:
    QTcpServer *m_server{nullptr};
    QList<IncomingConnection *> m_connections;
};
