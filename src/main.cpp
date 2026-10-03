#include "ui/MainWindow.h"
#include <QApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Ronia Trace Tool");
    QApplication::setOrganizationName("Ronia");

    // Load stylesheet if present next to the executable.
    QFile qss("style.qss");
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    }

    MainWindow w;
    w.show();
    return app.exec();
}