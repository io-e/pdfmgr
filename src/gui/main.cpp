#include <QElapsedTimer>
#include <QThread>
#include <QScrollArea>
#include <QScrollBar>
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
            // Tile cache: scrolling back to an already rendered area submits no
            // render job.
            const char* tiles = "skipped";
            if (auto* t = w.current()) {
                auto idle = [&] {
                    QElapsedTimer el; el.start();
                    do { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(5); }
                    while ((t->tilesInFlight() > 0 || el.elapsed() < 150) && el.elapsed() < 10000);
                };
                t->setZoom(3.0); idle();
                auto* vs = t->scrollArea()->verticalScrollBar();
                const quint64 first = t->tileJobs();
                vs->setValue(vs->maximum()); idle();
                const quint64 afterDown = t->tileJobs();
                vs->setValue(0); idle();
                vs->setValue(vs->maximum()); idle();
                const bool ok = first > 0 && afterDown > first && t->tileJobs() == afterDown && vs->maximum() > 0;
                if (!ok) std::printf("tiles: first=%llu down=%llu end=%llu max=%d\n", (unsigned long long)first,
                                     (unsigned long long)afterDown, (unsigned long long)t->tileJobs(), vs->maximum());
                tiles = ok ? "ok" : "FAIL";
                if (!ok) allOk = false;
                t->setZoom(1.0); idle();
            }
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
            // Async page op through the JobQueue: the tab is busy (actions
            // disabled) while it runs, the event loop keeps spinning, and the
            // result is applied when the job finishes.
            auto* t = w.current();
            bool started = t && w.runTask(t, "Smoke rotate", MainWindow::OpKind::ApplyToTab,
                [in = t->workingPath(), out = QDir::temp().filePath("pdfmgr-smoke-async.pdf")](const pdfcore::PageOps::Progress&) {
                    return pdfcore::PageOps::rotate(in, {0}, 90, out); },
                QDir::temp().filePath("pdfmgr-smoke-async.pdf"), {0});
            bool busyNow = t && t->isBusy();
            bool rejectsSecond = t && !w.runTask(t, "second", MainWindow::OpKind::ApplyToTab,
                [](const pdfcore::PageOps::Progress&) { return pdfcore::OpResult{}; });
            auto report = [&app, &allOk, t, started, busyNow, rejectsSecond, tabs, pages, apply, tiles] {
                bool async = started && busyNow && rejectsSecond && t && !t->isBusy()
                    && pdfcore::PageOps::rotations(t->workingPath()).value(0) == 180;
                if (!async) allOk = false;
                std::printf("SMOKE %s tabs=%d pages=%d apply=%s async=%s tiles=%s\n", allOk ? "OK" : "FAIL", tabs, pages, apply, async ? "ok" : "FAIL", tiles);
                std::fflush(stdout);
                app.exit(allOk ? 0 : 1);
            };
            if (!t) { report(); return; }
            auto* poll = new QTimer(&app);
            QObject::connect(poll, &QTimer::timeout, &app, [=, n = 0]() mutable {
                if (!t->isBusy() || ++n > 200) { poll->stop(); report(); }
            });
            poll->start(50);
        });
    }
    return app.exec();
}
