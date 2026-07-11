#include <QApplication>
#include <QFile>
#include <QDir>
#include "SystemMonitorV2.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("SysmonV2");

    const QString APP_COPYRIGHT = "(c) GnomeWorx 2026  Version 2.0.0";

    SystemMonitorV2 win;
    win.show();

    return app.exec();
}
