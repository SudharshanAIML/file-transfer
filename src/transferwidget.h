// transferwidget.h — progress card for one active transfer (outgoing or incoming)
#pragma once
#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QElapsedTimer>

enum class TransferDirection { Outgoing, Incoming };

class TransferWidget : public QWidget {
    Q_OBJECT
public:
    explicit TransferWidget(TransferDirection dir,
                            const QString &peerName,
                            qint64 totalBytes,
                            QWidget *parent = nullptr);

    void updateProgress(qint64 done, qint64 total, const QString &currentFile);
    void setFinished(bool success, const QString &message);

signals:
    void cancelRequested();

private:
    void updateSpeedLabel(qint64 done);

    QLabel      *m_titleLabel{nullptr};
    QLabel      *m_fileLabel{nullptr};
    QLabel      *m_speedLabel{nullptr};
    QLabel      *m_statusLabel{nullptr};
    QProgressBar *m_progress{nullptr};
    QPushButton  *m_cancelBtn{nullptr};

    qint64        m_totalBytes{0};
    qint64        m_lastDone{0};
    QElapsedTimer m_speedTimer;
    qint64        m_speedBytes{0};
};
