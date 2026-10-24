#include <QElapsedTimer>
#include <QThread>
#include <QScrollArea>
#include <QScrollBar>
#include <QMimeData>
#include <QUrl>
#include <QDropEvent>
#include <QListWidget>
#include "ThumbList.h"
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
    QCommandLineOption dnd("dnd-test", "Drop a PDF on the thumbnails and reorder pages, then quit and report (CI).");
    p.addOption(dnd);
    p.addPositionalArgument("files", "PDF files to open", "[files...]");
    p.process(app);
    MainWindow w;
    w.show();
    bool allOk = true;
    for (const auto& f : p.positionalArguments()) allOk &= w.openFile(f);
    if (p.isSet(dnd)) {
        // 1. A PDF dropped from a "file manager" on the thumbnail strip is
        //    inserted at the drop position. 2. Thumbnail move = reorder.
        QTimer::singleShot(500, &app, [&] {
            auto* t = w.current();
            auto wait = [&] {
                QElapsedTimer el; el.start();
                while (t->isBusy() && el.elapsed() < 10000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QThread::msleep(5); }
            };
            bool ok = t && t->pageCount() == 3;
            const int before = ok ? t->pageCount() : 0;
            QString extra = QDir::temp().filePath("pdfmgr-dnd-extra.pdf");
            ok = ok && pdfcore::PageOps::extract(t->workingPath(), {2}, extra).ok; // 1-page file
            if (ok) {
                auto* list = t->thumbnails();
                auto* mime = new QMimeData; mime->setUrls({QUrl::fromLocalFile(extra)});
                QPoint pos = list->visualItemRect(list->item(0)).bottomLeft() - QPoint(-5, 2); // lower half of page 1
                QDragEnterEvent enter(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(list->viewport(), &enter);
                QDragMoveEvent move(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(list->viewport(), &move);
                QDropEvent drop(pos, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(list->viewport(), &drop);
                wait();
                ok = enter.isAccepted() && move.isAccepted() && t->pageCount() == before + 1
                     && t->document()->pageText(1).contains("Page 3 Smoke");
                delete mime;
            }
            const bool dropOk = ok;
            ok = ok && w.movePages(t, {0}, 4); // first page to the end
            wait();
            ok = ok && t->document()->pageText(3).contains("Page 1 Smoke") && t->document()->pageText(0).contains("Page 3 Smoke");
            QFile::remove(extra);
            std::printf("DND %s drop=%s move=%s pages=%d\n", ok ? "OK" : "FAIL", dropOk ? "ok" : "FAIL", ok ? "ok" : "FAIL", t ? t->pageCount() : -1);
            std::fflush(stdout);
            app.exit(ok ? 0 : 1);
        });
    } else if (p.isSet(smoke)) {
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
