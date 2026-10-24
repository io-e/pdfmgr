#include <catch2/catch_test_macros.hpp>
#include "AtomicWrite.h"
#include "Fixtures.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
using namespace pdfcore;
using fixtures::tmpFile;

static QByteArray readAll(const QString& f) { QFile r(f); r.open(QIODevice::ReadOnly); return r.readAll(); }
static int backups(const QString& target) {
    QFileInfo fi(target);
    return int(QDir(fi.absolutePath()).entryList({fi.fileName() + ".bak-*"}).size());
}

TEST_CASE("replaceFileSafely replaces and leaves no backup", "[io][applyWorkingCopy]") {
    auto target = tmpFile("rep-target.pdf"), repl = tmpFile("rep-new.pdf");
    REQUIRE(atomicWrite(target, "old"));
    REQUIRE(atomicWrite(repl, "new"));
    QString err;
    REQUIRE(replaceFileSafely(repl, target, &err));
    CHECK(readAll(target) == "new");
    CHECK_FALSE(QFile::exists(repl));
    CHECK(backups(target) == 0);
}

TEST_CASE("replaceFileSafely never loses the target when the replacement is missing", "[io][applyWorkingCopy]") {
    auto target = tmpFile("rep-keep.pdf");
    REQUIRE(atomicWrite(target, "edits"));
    QString err;
    CHECK_FALSE(replaceFileSafely(tmpFile("does-not-exist.pdf"), target, &err, 50));
    CHECK_FALSE(err.isEmpty());
    CHECK(readAll(target) == "edits"); // working copy (and its edits) intact
    CHECK(backups(target) == 0);
}

TEST_CASE("removeWithRetry", "[io]") {
    auto f = tmpFile("rm.bin");
    REQUIRE(atomicWrite(f, "x"));
    CHECK(removeWithRetry(f, 100));
    CHECK_FALSE(QFile::exists(f));
    CHECK(removeWithRetry(f, 100)); // already gone = success
}
