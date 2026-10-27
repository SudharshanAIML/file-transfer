// transferclient.h — TCP client that sends files to a remote peer
#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QFile>
#include <QTimer>
#include "transfersession.h"
#include "protocol.h"

class TransferClient : public QObject {
    Q_OBJECT
public:
    explicit TransferClient(QObject *parent = nullptr);
    ~TransferClient() override;

    // Start a transfer; connect to host:port and send files
    void sendFiles(const QHostAddress &host, quint16 port,
                   const QString &localDeviceName,
                   const QList<FileEntry> &files);

    void cancel();

signals:
    void progressUpdate(qint64 bytesSent, qint64 totalBytes, const QString &currentFile);
    void transferFinished(bool success, const QString &message);
    // Remote rejected our request
    void transferRejected();

private slots:
    void onConnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError err);
    void onDisconnected();
    void pumpData();     // called by QTimer to keep feeding data asynchronously

private:
    void sendTransferRequest();
    void startNextFile();
    void sendFileHeader(int idx);
    QByteArray buildFrame(MsgType type, const QByteArray &payload = {});

    enum class State {
        Connecting,
        WaitingForAcceptance,
        Sending,
        Done
    };

    QTcpSocket  *m_socket{nullptr};
    QTimer      *m_pumpTimer{nullptr};

    QString              m_deviceName;
    QList<FileEntry>     m_files;
    qint64               m_totalBytes{0};
    qint64               m_totalSent{0};

    State         m_state{State::Connecting};
    int           m_currentFileIdx{-1};
    qint64        m_currentFileSent{0};
    QFile         m_inFile;

    QByteArray    m_recvBuffer;
    bool          m_cancelled{false};
};
