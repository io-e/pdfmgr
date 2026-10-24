// End-to-end print test against a real "print to PDF" queue.
//   Linux:   CUPS-PDF queue, printed through CupsBackend (lp).
//   Windows: "Microsoft Print to PDF" on a file port, through SumatraBackend.
// Configuration (environment):
//   PDFMGR_TEST_PRINTER     queue name (required; test is SKIPPED when unset)
//   PDFMGR_TEST_PRINT_OUT   output directory (CUPS-PDF) or fixed output file
//                           (Windows file port)
//   PDFMGR_SUMATRA          path to SumatraPDF.exe (Windows)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include "PrintService.h"
#include "CupsBackend.h"
#include "SumatraBackend.h"
#include "PageOps.h"
#include "Document.h"
#include "Fixtures.h"
using namespace pdfcore;

namespace {
struct Config { QString printer, out, sumatra; };
Config config() {
    return {qEnvironmentVariable("PDFMGR_TEST_PRINTER"), qEnvironmentVariable("PDFMGR_TEST_PRINT_OUT"),
            qEnvironmentVariable("PDFMGR_SUMATRA")};
}
std::unique_ptr<IPrintBackend> backend(const Config& c) {
#ifdef Q_OS_WIN
    return std::make_unique<SumatraBackend>(c.sumatra.isEmpty() ? SumatraBackend::defaultExePath() : c.sumatra);
#else
    Q_UNUSED(c);
    return std::make_unique<CupsBackend>();
#endif
}
QFileInfoList pdfsIn(const QString& dir) {
    return QDir(dir).entryInfoList({"*.pdf"}, QDir::Files, QDir::Time);
}
// Waits until a new/updated PDF appears and its size is stable and parseable.
QString waitForOutput(const QString& out, const QDateTime& since, int timeoutMs) {
    QElapsedTimer t; t.start();
    qint64 lastSize = -1;
    while (t.elapsed() < timeoutMs) {
        QString cand;
        if (QFileInfo(out).isDir()) {
            for (const auto& fi : pdfsIn(out)) if (fi.lastModified() >= since) { cand = fi.absoluteFilePath(); break; }
        } else if (QFileInfo::exists(out)) cand = out;
        if (!cand.isEmpty()) {
            qint64 sz = QFileInfo(cand).size();
            if (sz > 0 && sz == lastSize && PageOps::pageCount(cand) > 0) return cand;
            lastSize = sz;
        }
        QCoreApplication::processEvents();
        QThread::msleep(250);
    }
    return {};
}
}

TEST_CASE("print to a PDF printer and verify the output", "[print][integration]") {
    const Config c = config();
    if (c.printer.isEmpty() || c.out.isEmpty())
        SKIP("No PDF printer configured: set PDFMGR_TEST_PRINTER and PDFMGR_TEST_PRINT_OUT "
             "(Linux: CUPS-PDF queue + output dir; Windows: 'Microsoft Print to PDF' on a file port + file path)");
#ifdef Q_OS_WIN
    if (!QFileInfo::exists(c.sumatra.isEmpty() ? SumatraBackend::defaultExePath() : c.sumatra))
        SKIP("SumatraPDF.exe not found: set PDFMGR_SUMATRA");
#endif
    // Source: 3 landscape pages (842 x 595 pt), so orientation is checkable.
    const QString src = fixtures::tmpFile("print-src.pdf");
    fixtures::writePdf(src, 3, "Printed", QSizeF(842, 595));
    if (!QFileInfo(c.out).isDir()) QFile::remove(c.out);
    const QDateTime since = QDateTime::currentDateTime().addSecs(-2);

    PrintService svc{backend(c)};
    QSignalSpy spy(&svc, &PrintService::jobFinished);
    PrintJob j; j.file = src; j.printer = c.printer;
    j.orientation = PrintJob::Orientation::Landscape;
    j.scaling = PrintJob::Scaling::Fit;
    svc.enqueue(j);
    REQUIRE(spy.wait(120000));
    INFO("backend stderr: " << spy[0][3].toString().toStdString());
    REQUIRE(spy[0][1].toBool());

    const QString out = waitForOutput(c.out, since, 90000);
    INFO("output location: " << c.out.toStdString());
    REQUIRE_FALSE(out.isEmpty());
    CHECK(PageOps::pageCount(out) == 3);
    QString err; auto d = Document::open(out, &err);
    REQUIRE(d);
    for (int p = 0; p < d->pageCount(); ++p) {
        QSizeF s = d->pageSize(p); // includes /Rotate
        INFO("page " << p << " size " << s.width() << "x" << s.height());
        CHECK(s.width() > s.height()); // landscape preserved
    }
    CHECK(d->pageText(0).contains("Page 1 Printed"));
}

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
