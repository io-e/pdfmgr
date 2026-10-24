#include <QCoreApplication>
#include <catch2/catch_test_macros.hpp>
#include "SumatraBackend.h"
#include "CupsBackend.h"
#include "PrintService.h"
#include "Fixtures.h"
#include <QEventLoop>
#include <QTimer>
#include <QSignalSpy>
using namespace pdfcore;

TEST_CASE("Sumatra settings string", "[print][sumatra]") {
    PrintJob j;
    CHECK(SumatraBackend::settingsString(j) == "shrink");
    j.pageRange = "1-3, 5"; j.scaling = PrintJob::Scaling::Fit; j.orientation = PrintJob::Orientation::Landscape;
    j.duplex = PrintJob::Duplex::DuplexLong; j.color = PrintJob::Color::Monochrome; j.paper = "A4"; j.bin = 2; j.copies = 3;
    CHECK(SumatraBackend::settingsString(j) == "1-3,5,fit,landscape,duplexlong,monochrome,paper=A4,bin=2,3x");
    PrintJob k; k.pageRange = "odd"; k.scaling = PrintJob::Scaling::NoScale; k.duplex = PrintJob::Duplex::Simplex; k.color = PrintJob::Color::Color;
    CHECK(SumatraBackend::settingsString(k) == "odd,noscale,simplex,color");
}
TEST_CASE("Sumatra arguments", "[print][sumatra]") {
    SumatraBackend b("C:/App/SumatraPDF.exe");
    PrintJob j; j.file = "C:/tmp/a b é.pdf"; j.printer = "HP LaserJet 4000"; j.copies = 2;
    auto a = b.arguments(j);
    REQUIRE(a.size() == 6);
    CHECK(a[0] == "-print-to"); CHECK(a[1] == "HP LaserJet 4000");
    CHECK(a[2] == "-print-settings"); CHECK(a[3] == "shrink,2x");
    CHECK(a[4] == "-silent"); CHECK(a.last().endsWith("a b é.pdf"));
    j.printer.clear();
    CHECK(b.arguments(j).first() == "-print-to-default");
}
TEST_CASE("CUPS lp arguments", "[print][cups]") {
    CupsBackend b;
    PrintJob j; j.file = "/tmp/a b.pdf";
    CHECK(b.arguments(j) == QStringList{"--", "/tmp/a b.pdf"});
    j.printer = "Office"; j.copies = 2; j.pageRange = "1-3,5"; j.scaling = PrintJob::Scaling::Fit;
    j.orientation = PrintJob::Orientation::Landscape; j.duplex = PrintJob::Duplex::DuplexShort;
    j.color = PrintJob::Color::Monochrome; j.paper = "A4"; j.bin = 1;
    CHECK(b.arguments(j) == QStringList{"-d", "Office", "-n", "2", "-o", "page-ranges=1-3,5", "-o", "fit-to-page",
        "-o", "landscape", "-o", "sides=two-sided-short-edge", "-o", "print-color-mode=monochrome",
        "-o", "media=A4", "-o", "InputSlot=1", "--", "/tmp/a b.pdf"});
    j.pageRange = "even";
    CHECK(b.arguments(j).contains("page-set=even"));
}

// Fake backend: re-runs this test binary in --fake-backend mode (see main.cpp),
// so the service is tested without a printer, a shell or the qpdf CLI, on any OS.
struct FakeBackend : IPrintBackend {
    QString mode; mutable QStringList lastFiles;
    explicit FakeBackend(QString m) : mode(std::move(m)) {}
    QString name() const override { return "fake"; }
    QString program() const override { return QCoreApplication::applicationFilePath(); }
    QStringList arguments(const PrintJob& j) const override { lastFiles << j.file; return {"--fake-backend", mode, j.file}; }
};

static QList<QList<QVariant>> runAll(PrintService& s, int expected) {
    QSignalSpy spy(&s, &PrintService::jobFinished);
    QEventLoop loop; QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    QObject::connect(&s, &PrintService::jobFinished, &loop, [&] { if (spy.size() >= expected) loop.quit(); });
    loop.exec();
    return spy;
}

TEST_CASE("PrintService prints a temp copy, in order", "[print][service]") {
    auto* fb = new FakeBackend("check");
    PrintService s{std::unique_ptr<IPrintBackend>(fb)};
    PrintJob j; j.file = fixtures::path("three.pdf");
    auto id1 = s.enqueue(j); auto id2 = s.enqueue(j);
    auto res = runAll(s, 2);
    REQUIRE(res.size() == 2);
    CHECK(res[0][0].toULongLong() == id1); CHECK(res[1][0].toULongLong() == id2);
    CHECK(res[0][1].toBool()); CHECK(res[0][3].toString().contains("checked"));
    REQUIRE(fb->lastFiles.size() == 2);
    CHECK(fb->lastFiles[0] != j.file);              // printed the temp copy
    CHECK_FALSE(QFile::exists(fb->lastFiles[0]));   // and cleaned it up
}
TEST_CASE("PrintService reports exit code, stderr and timeout", "[print][service]") {
    PrintService s{std::make_unique<FakeBackend>("fail")};
    PrintJob j; j.file = fixtures::path("one.pdf");
    s.enqueue(j);
    auto r = runAll(s, 1);
    REQUIRE(r.size() == 1);
    CHECK_FALSE(r[0][1].toBool()); CHECK(r[0][2].toInt() == 3); CHECK(r[0][3].toString().contains("boom"));

    PrintService t{std::make_unique<FakeBackend>("sleep")};
    t.setTimeoutMs(300);
    t.enqueue(j);
    auto r2 = runAll(t, 1);
    REQUIRE(r2.size() == 1);
    CHECK_FALSE(r2[0][1].toBool()); CHECK(r2[0][3].toString().contains("timeout"));

    PrintJob bad; bad.file = fixtures::path("garbage.pdf");
    PrintService u{std::make_unique<FakeBackend>("ok")};
    u.enqueue(bad);
    auto r3 = runAll(u, 1);
    REQUIRE(r3.size() == 1);
    CHECK_FALSE(r3[0][1].toBool()); CHECK(r3[0][3].toString().contains("temp save failed"));
}
TEST_CASE("default backend per platform", "[print]") {
#ifdef Q_OS_WIN
    CHECK(PrintService::defaultBackend()->name() == "SumatraPDF");
#else
    CHECK(PrintService::defaultBackend()->name() == "CUPS");
#endif
}
