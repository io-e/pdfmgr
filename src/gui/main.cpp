#include "MainWindow.h"
#include "DocumentTab.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QTabWidget>
#include <cstdio>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("PDF Manager");
    QApplication::setApplicationVersion("0.1.0");
    QCommandLineParser p;
    p.addHelpOption(); p.addVersionOption();
    QCommandLineOption smoke("smoke-test", "Open files, render, then quit and report (CI).");
    p.addOption(smoke);
    p.addPositionalArgument("files", "PDF files to open", "[files...]");
    p.process(app);
    MainWindow w;
    w.show();
    bool allOk = true;
    for (const auto& f : p.positionalArguments()) allOk &= w.openFile(f);
    if (p.isSet(smoke)) {
        QTimer::singleShot(1500, &app, [&] {
            int tabs = w.tabs()->count();
            int pages = w.current() ? w.current()->pageCount() : 0;
            std::printf("SMOKE %s tabs=%d pages=%d\n", allOk ? "OK" : "FAIL", tabs, pages);
            std::fflush(stdout);
            app.exit(allOk ? 0 : 1);
        });
    }
    return app.exec();
}
