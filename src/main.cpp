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

// #include "MainWindow.h"
// #include <QApplication>
// #include <QCommandLineParser>
// #include <QFileInfo>

// int main(int argc, char** argv) {
//     QApplication app(argc, argv);
//     QApplication::setApplicationName("RoniaTraceTool");

//     QCommandLineParser p;
//     p.addHelpOption();
//     p.addPositionalArgument("trace", "Optional trace.log to open on startup");
//     p.process(app);

//     rt::MainWindow w;
//     w.show();

//     const QStringList args = p.positionalArguments();
//     if (!args.isEmpty() && QFileInfo::exists(args.first())) {
//         QMetaObject::invokeMethod(&w, "openFile");  // fallback: let user pick
//         // If you want to auto-open, replace the line above with a slot call
//         // that takes a path. For now we just show the window; use File → Open.
//     }
//     return app.exec();
// }
