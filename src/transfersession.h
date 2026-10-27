// transfersession.h — data structures shared between client and server
#pragma once
#include <QString>
#include <QList>
#include <QHostAddress>

// Describes one file in a transfer batch
struct FileEntry {
    QString  displayName;   // user-visible filename
    QString  absolutePath;  // full path on sender's disk
    QString  relativePath;  // path to recreate in destination folder
    qint64   size{0};       // bytes
};

// Describes an incoming transfer request (used on receiver side)
struct IncomingTransfer {
    QString         senderName;
    QHostAddress    senderAddress;
    QList<FileEntry> files;
    qint64          totalBytes{0};
};
