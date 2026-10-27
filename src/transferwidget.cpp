// transferwidget.cpp
#include "transferwidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

static QString humanSize(qint64 bytes)
{
    if (bytes < 1024)           return QString("%1 B").arg(bytes);
    if (bytes < 1024*1024)      return QString("%1 KB").arg(bytes/1024);
    if (bytes < 1024LL*1024*1024) return QString("%1 MB").arg(bytes/(1024*1024));
    return QString("%1 GB").arg(bytes/(1024LL*1024*1024));
}

TransferWidget::TransferWidget(TransferDirection dir,
                               const QString &peerName,
                               qint64 totalBytes,
                               QWidget *parent)
    : QWidget(parent), m_totalBytes(totalBytes)
{
    setStyleSheet(R"(
        TransferWidget {
            background: #1e1e3a;
            border: 1px solid #2a2a5a;
            border-radius: 12px;
            padding: 4px;
        }
        QLabel { color: #c0c0e0; font-size: 13px; }
        QLabel#titleLabel { color: #a0aaff; font-size: 14px; font-weight: bold; }
        QLabel#speedLabel { color: #80ffa0; font-size: 12px; }
        QLabel#statusLabel { font-size: 12px; }
        QProgressBar {
            background: #0d0d20;
            border: none;
            border-radius: 6px;
            height: 10px;
            text-align: center;
            color: transparent;
        }
        QProgressBar::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                         stop:0 #667eea, stop:1 #764ba2);
            border-radius: 6px;
        }
        QPushButton {
            background: #3a1a2a;
            color: #ff8aaa;
            border: 1px solid #8a3a5a;
            border-radius: 6px;
            padding: 4px 12px;
            font-size: 12px;
        }
        QPushButton:hover { background: #4a2a3a; }
    )");

    setMinimumHeight(110);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(14, 10, 14, 10);
    vbox->setSpacing(6);

    // Title row
    auto *titleRow = new QHBoxLayout;
    QString arrow  = (dir == TransferDirection::Outgoing) ? "↑ Sending to" : "↓ Receiving from";
    m_titleLabel   = new QLabel(QString("%1  %2").arg(arrow).arg(peerName));
    m_titleLabel->setObjectName("titleLabel");
    titleRow->addWidget(m_titleLabel);
    titleRow->addStretch();

    m_cancelBtn = new QPushButton("✕ Cancel");
    connect(m_cancelBtn, &QPushButton::clicked, this, &TransferWidget::cancelRequested);
    titleRow->addWidget(m_cancelBtn);
    vbox->addLayout(titleRow);

    // File name
    m_fileLabel = new QLabel("Preparing…");
    m_fileLabel->setStyleSheet("color: #8888cc; font-size: 12px;");
    vbox->addWidget(m_fileLabel);

    // Progress bar
    m_progress = new QProgressBar;
    m_progress->setRange(0, 1000); // use permil for smoother updates
    m_progress->setValue(0);
    m_progress->setFixedHeight(10);
    vbox->addWidget(m_progress);

    // Speed + total
    auto *infoRow = new QHBoxLayout;
    m_speedLabel  = new QLabel("0 KB/s");
    m_speedLabel->setObjectName("speedLabel");
    infoRow->addWidget(m_speedLabel);
    infoRow->addStretch();

    m_statusLabel = new QLabel(QString("0 / %1").arg(humanSize(totalBytes)));
    m_statusLabel->setObjectName("statusLabel");
    infoRow->addWidget(m_statusLabel);
    vbox->addLayout(infoRow);

    m_speedTimer.start();
}

void TransferWidget::updateProgress(qint64 done, qint64 total, const QString &currentFile)
{
    m_totalBytes = total;
    m_fileLabel->setText("📄  " + currentFile);
    m_progress->setValue(total > 0 ? int(done * 1000 / total) : 0);
    m_statusLabel->setText(QString("%1 / %2").arg(humanSize(done)).arg(humanSize(total)));
    updateSpeedLabel(done);
}

void TransferWidget::updateSpeedLabel(qint64 done)
{
    qint64 elapsed = m_speedTimer.elapsed();
    if (elapsed < 500) return; // update speed label at most every 500ms

    qint64 delta = done - m_lastDone;
    double bps   = delta * 1000.0 / elapsed; // bytes/sec

    QString speed;
    if (bps < 1024)         speed = QString("%1 B/s").arg(int(bps));
    else if (bps < 1024*1024) speed = QString("%1 KB/s").arg(int(bps/1024));
    else                    speed = QString("%1 MB/s").arg(int(bps/(1024*1024)));

    m_speedLabel->setText(speed);
    m_lastDone = done;
    m_speedTimer.restart();
}

void TransferWidget::setFinished(bool success, const QString &message)
{
    m_cancelBtn->setEnabled(false);
    m_fileLabel->setText(message);
    if (success) {
        m_progress->setValue(1000);
        m_speedLabel->setText("✅ Done");
        m_statusLabel->setText(humanSize(m_totalBytes));
        setStyleSheet(styleSheet() +
            "TransferWidget { border-color: #208020; }");
    } else {
        m_speedLabel->setText("❌ Failed");
        setStyleSheet(styleSheet() +
            "TransferWidget { border-color: #802020; }");
    }
}
