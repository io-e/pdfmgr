#include "MainWindow.h"
#include "DocumentTab.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QTabWidget>
#include <cstdio>
#include <QDir>
#include <QFile>
#include "PageOps.h"

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
            // applyWorkingCopy: a bad result must be rejected without losing the
            // document; a good one must replace the working copy.
            const char* apply = "skipped";
            if (auto* t = w.current()) {
                QString bad = QDir::temp().filePath("pdfmgr-smoke-bad.pdf"), good = QDir::temp().filePath("pdfmgr-smoke-good.pdf");
                { QFile f(bad); f.open(QIODevice::WriteOnly); f.write("not a pdf"); }
                QString err;
                bool rejected = !t->applyWorkingCopy(bad, &err) && t->pageCount() == pages && QFile::exists(t->workingPath());
                QFile::remove(bad);
                bool applied = pdfcore::PageOps::rotate(t->workingPath(), {0}, 90, good).ok
                    && t->applyWorkingCopy(good, &err, {0}) && t->pageCount() == pages && t->isModified()
                    && pdfcore::PageOps::rotations(t->workingPath()).value(0) == 90;
                apply = rejected && applied ? "ok" : "FAIL";
                if (!(rejected && applied)) allOk = false;
            }
            std::printf("SMOKE %s tabs=%d pages=%d apply=%s\n", allOk ? "OK" : "FAIL", tabs, pages, apply);
            std::fflush(stdout);
            app.exit(allOk ? 0 : 1);
        });
    }
    return app.exec();
}
