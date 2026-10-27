// main.cpp
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("LANShare");
    app.setOrganizationName("LANShare");
    app.setApplicationVersion("1.0.0");

    // Use a clean system font
    QFont font = app.font();
    font.setFamily("Inter, Segoe UI, Ubuntu, Helvetica Neue, sans-serif");
    font.setPixelSize(13);
    app.setFont(font);

    // Dark palette for system dialogs
    QPalette pal;
    pal.setColor(QPalette::Window,          QColor(0x13, 0x13, 0x1f));
    pal.setColor(QPalette::WindowText,      QColor(0xd0, 0xd0, 0xff));
    pal.setColor(QPalette::Base,            QColor(0x1a, 0x1a, 0x30));
    pal.setColor(QPalette::AlternateBase,   QColor(0x1e, 0x1e, 0x3a));
    pal.setColor(QPalette::Text,            QColor(0xd0, 0xd0, 0xff));
    pal.setColor(QPalette::Button,          QColor(0x2a, 0x2a, 0x5a));
    pal.setColor(QPalette::ButtonText,      QColor(0xd0, 0xd0, 0xff));
    pal.setColor(QPalette::Highlight,       QColor(0x66, 0x7e, 0xea));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(pal);

    MainWindow w;
    w.show();

    return app.exec();
}
