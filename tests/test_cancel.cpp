#include <catch2/catch_test_macros.hpp>
#include "PageOps.h"
#include "Fixtures.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
using namespace pdfcore;

namespace {
QByteArray sha(const QString& f) { QFile x(f); x.open(QIODevice::ReadOnly); return QCryptographicHash::hash(x.readAll(), QCryptographicHash::Sha256); }
// Files in `dir` other than the ones that existed before (temp/partial leftovers).
QStringList newFiles(const QString& dir, const QStringList& before) {
    QStringList out;
    for (const auto& f : QDir(dir).entryList(QDir::Files)) if (!before.contains(f)) out << f;
    return out;
}
// Cancels when `done` reaches `at` (or on the first write-phase call if at < 0).
PageOps::Progress cancelAt(int at, int* calls, int* sawTotal) {
    return [=](int done, int total) {
        ++*calls; *sawTotal = total;
        return at >= 0 ? done < at : done <= total - 100;
    };
}
}

TEST_CASE("merge: cancel between pages leaves nothing behind", "[cancel][pageops]") {
    const QString a = fixtures::tmpFile("cancel-a.pdf"), b = fixtures::tmpFile("cancel-b.pdf");
    fixtures::writePdf(a, 40, "A"); fixtures::writePdf(b, 60, "B");
    const QByteArray ha = sha(a), hb = sha(b);
    const QString dir = QFileInfo(a).absolutePath(), out = fixtures::tmpFile("cancel-merge.pdf");
    QFile::remove(out);
    const QStringList before = QDir(dir).entryList(QDir::Files);
    int calls = 0, total = 0;
    auto r = PageOps::merge({a, b}, out, cancelAt(50, &calls, &total));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "canceled");
    CHECK(r.outputs.isEmpty());
    CHECK(total == 100 + 100); // 100 pages + 100 write percent
    CHECK(calls == 50);        // stopped at page 50, never reached the write
    CHECK_FALSE(QFile::exists(out));
    CHECK(newFiles(dir, before).isEmpty());
    CHECK(sha(a) == ha); CHECK(sha(b) == hb);
    // Not canceled: same call succeeds, progress reaches the end.
    int last = 0;
    auto ok = PageOps::merge({a, b}, out, [&](int d, int t) { last = d; CHECK(t == 200); return true; });
    REQUIRE(ok.ok);
    CHECK(last == 200);
    CHECK(PageOps::pageCount(out) == 100);
}

TEST_CASE("merge: cancel while QPDF writes objects", "[cancel][pageops]") {
    const QString a = fixtures::tmpFile("cancel-w.pdf");
    fixtures::writePdf(a, 300, "W");
    const QString dir = QFileInfo(a).absolutePath(), out = fixtures::tmpFile("cancel-w-out.pdf");
    QFile::remove(out);
    const QStringList before = QDir(dir).entryList(QDir::Files);
    int calls = 0, total = 0;
    auto r = PageOps::merge({a, a}, out, cancelAt(-1, &calls, &total));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "canceled");
    CHECK(calls > 600); // every page passed, then canceled inside the writer
    CHECK_FALSE(QFile::exists(out));
    CHECK(newFiles(dir, before).isEmpty());
}

TEST_CASE("rotate in place: cancel keeps the original untouched", "[cancel][pageops]") {
    const QString f = fixtures::tmpFile("cancel-rot.pdf");
    fixtures::writePdf(f, 120, "R");
    const QByteArray h = sha(f);
    const QString dir = QFileInfo(f).absolutePath();
    const QStringList before = QDir(dir).entryList(QDir::Files);
    for (int at : {10, -1}) { // between pages, then during the write
        int calls = 0, total = 0;
        auto r = PageOps::rotate(f, {}, 90, f, cancelAt(at, &calls, &total));
        INFO("cancel at " << at);
        CHECK_FALSE(r.ok);
        CHECK(r.error == "canceled");
        CHECK(total == 220);
        CHECK(sha(f) == h);
        CHECK(newFiles(dir, before).isEmpty());
    }
    CHECK(PageOps::rotations(f).value(0) == 0);
    REQUIRE(PageOps::rotate(f, {}, 90, f, [](int, int) { return true; }).ok);
    CHECK(PageOps::rotations(f).value(119) == 90);
}
