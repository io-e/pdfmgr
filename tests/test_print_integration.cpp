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
#include <catch2/generators/catch_generators.hpp>
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
#include <QImage>
#include "PrintService.h"
#include "CupsBackend.h"
#include "SumatraBackend.h"
#include "PageOps.h"
#include "Document.h"
#include "Fixtures.h"
using namespace pdfcore;

namespace {
// Bounding box of dark pixels.
QRect inkBox(const QImage& img) {
    QRect box;
    const QImage g = img.convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < g.height(); ++y) {
        const uchar* row = g.constScanLine(y);
        for (int x = 0; x < g.width(); ++x) if (row[x] < 128) box |= QRect(x, y, 1, 1);
    }
    return box;
}
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
QString waitForOutput(const QString& out, const QStringList& before, int timeoutMs) {
    QElapsedTimer t; t.start();
    qint64 lastSize = -1;
    while (t.elapsed() < timeoutMs) {
        QString cand;
        if (QFileInfo(out).isDir()) {
            for (const auto& fi : pdfsIn(out)) if (!before.contains(fi.absoluteFilePath())) { cand = fi.absoluteFilePath(); break; }
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
    QStringList before; // files already in the output dir
    if (QFileInfo(c.out).isDir()) for (const auto& fi : pdfsIn(c.out)) before << fi.absoluteFilePath();

    PrintService svc{backend(c)};
    QSignalSpy spy(&svc, &PrintService::jobFinished);
    PrintJob j; j.file = src; j.printer = c.printer;
    const auto orient = GENERATE(PrintJob::Orientation::Auto, PrintJob::Orientation::Landscape);
    INFO("requested orientation: " << (orient == PrintJob::Orientation::Auto ? "auto" : "landscape"));
    j.orientation = orient;
    j.scaling = PrintJob::Scaling::Fit;
    svc.enqueue(j);
    REQUIRE(spy.wait(120000));
    INFO("backend stderr: " << spy[0][3].toString().toStdString());
    REQUIRE(spy[0][1].toBool());

    const QString out = waitForOutput(c.out, before, 90000);
    INFO("output location: " << c.out.toStdString());
    REQUIRE_FALSE(out.isEmpty());
    CHECK(PageOps::pageCount(out) == 3);
    QString err; auto d = Document::open(out, &err);
    REQUIRE(d);
    // Orientation: the content must still read as landscape. Drivers may emit
    // landscape sheets, or portrait sheets with the content turned 90 degrees
    // (what a physical printer does). The source page holds one line of text,
    // so the box around the printed ink tells which: a turned line is taller
    // than wide. Ink is measured on a render, because some drivers (SumatraPDF
    // through GDI -> Microsoft Print to PDF) rasterize and keep no text layer.
    for (int p = 0; p < d->pageCount(); ++p) {
        QSizeF s = d->pageSize(p); // includes /Rotate
        QRect ink = inkBox(d->render(p, 0.5));
        INFO("page " << p << " size " << s.width() << "x" << s.height()
             << ", ink box " << ink.width() << "x" << ink.height());
        REQUIRE_FALSE(ink.isEmpty());
        const bool sheetLandscape = s.width() > s.height();
        const bool textTurned = ink.height() > ink.width();
        CHECK(sheetLandscape != textTurned); // content is landscape either way
    }
    const QString text = d->pageText(0);
    if (text.trimmed().isEmpty()) WARN("output has no text layer (rasterized by the driver); text check skipped");
    else CHECK(text.contains("Page 1 Printed"));
}

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
