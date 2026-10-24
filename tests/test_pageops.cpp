#include <catch2/catch_test_macros.hpp>
#include "PageOps.h"
#include "Document.h"
#include "AtomicWrite.h"
#include "Fixtures.h"
#include <QFile>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <set>
#include <string>
using namespace pdfcore;
using fixtures::path; using fixtures::tmpFile; using fixtures::qpdfCheck;

static void requireValid(const QString& f) { QString o; int rc = qpdfCheck(f, &o); INFO(o.toStdString()); REQUIRE(rc == 0); }
static QString firstLine(const QString& f, int page) { return Document::open(f)->pageText(page).section('\n', 0, 0).trimmed(); }

TEST_CASE("merge", "[pageops]") {
    auto out = tmpFile("merge.pdf");
    auto r = PageOps::merge({path("one.pdf"), path("three.pdf"), path("unicode é.pdf")}, out);
    REQUIRE(r.ok);
    requireValid(out);
    CHECK(PageOps::pageCount(out) == 6);
    CHECK(firstLine(out, 0) == "Page 1 Alpha");
    CHECK(firstLine(out, 3) == "Page 3 Bravo");
    CHECK(firstLine(out, 5) == "Page 2 Echo");
}
TEST_CASE("split", "[pageops]") {
    auto r = PageOps::split(path("five.pdf"), tmpFile("split"), 2);
    REQUIRE(r.ok);
    REQUIRE(r.outputs.size() == 3);
    for (auto& f : r.outputs) requireValid(f);
    CHECK(PageOps::pageCount(r.outputs[0]) == 2);
    CHECK(PageOps::pageCount(r.outputs[2]) == 1);
    CHECK(firstLine(r.outputs[2], 0) == "Page 5 Charlie");
}
TEST_CASE("extract", "[pageops]") {
    auto out = tmpFile("extract.pdf");
    auto r = PageOps::extract(path("objstm.pdf"), {4, 1}, out);
    REQUIRE(r.ok);
    requireValid(out);
    CHECK(PageOps::pageCount(out) == 2);
    CHECK(firstLine(out, 0) == "Page 5 Charlie");
    CHECK(firstLine(out, 1) == "Page 2 Charlie");
    CHECK_FALSE(PageOps::extract(path("five.pdf"), {9}, tmpFile("bad.pdf")).ok);
}
TEST_CASE("reorder", "[pageops]") {
    auto out = tmpFile("reorder.pdf");
    auto r = PageOps::reorder(path("three.pdf"), {2, 0, 1}, out);
    REQUIRE(r.ok);
    requireValid(out);
    CHECK(firstLine(out, 0) == "Page 3 Bravo");
    CHECK(firstLine(out, 1) == "Page 1 Bravo");
}
TEST_CASE("rotate via /Rotate", "[pageops]") {
    auto out = tmpFile("rotate.pdf");
    REQUIRE(PageOps::rotate(path("three.pdf"), {1}, 90, out).ok);
    requireValid(out);
    CHECK(PageOps::rotations(out) == QList<int>{0, 90, 0});
    auto out2 = tmpFile("rotate2.pdf");
    REQUIRE(PageOps::rotate(out, {}, -90, out2).ok);
    requireValid(out2);
    CHECK(PageOps::rotations(out2) == QList<int>{270, 0, 270});
    auto d = Document::open(out);
    CHECK(int(d->pageSize(1).width()) == 842); // MuPDF honours /Rotate
    CHECK_FALSE(PageOps::rotate(path("three.pdf"), {}, 45, tmpFile("r45.pdf")).ok);
}
TEST_CASE("in-place op on same path is atomic", "[pageops]") {
    auto f = tmpFile("inplace.pdf");
    QFile::remove(f); QFile::copy(path("three.pdf"), f);
    REQUIRE(PageOps::rotate(f, {0}, 180, f).ok);
    requireValid(f);
    CHECK(PageOps::rotations(f).value(0) == 180);
}
TEST_CASE("errors are reported, not thrown", "[pageops]") {
    auto r = PageOps::merge({path("garbage.pdf")}, tmpFile("g.pdf"));
    CHECK_FALSE(r.ok); CHECK_FALSE(r.error.isEmpty());
    CHECK_FALSE(QFile::exists(tmpFile("g.pdf")));
}
TEST_CASE("parseRange", "[pageops]") {
    CHECK(PageOps::parseRange("1-3,5", 5) == QList<int>{0, 1, 2, 4});
    CHECK_FALSE(PageOps::parseRange("0", 5));
    CHECK_FALSE(PageOps::parseRange("4-2", 5));
    CHECK_FALSE(PageOps::parseRange("6", 5));
    CHECK_FALSE(PageOps::parseRange("", 5));
}
TEST_CASE("atomicWrite", "[io]") {
    auto f = tmpFile("atomic.bin");
    REQUIRE(atomicWrite(f, "first"));
    REQUIRE(atomicWrite(f, "second"));
    QFile r(f); r.open(QIODevice::ReadOnly); CHECK(r.readAll() == "second");
    QString err; CHECK_FALSE(atomicWrite("/nonexistent-dir/x.pdf", "x", &err)); CHECK_FALSE(err.isEmpty());
}

TEST_CASE("merge keeps AcroForm fields and outlines", "[pageops][forms]") {
    auto out = tmpFile("merge-form.pdf");
    // Same form twice: the second copy's field name conflicts and must be renamed, not dropped.
    auto r = PageOps::merge({path("form.pdf"), path("three.pdf"), path("form.pdf")}, out);
    REQUIRE(r.ok);
    requireValid(out);
    QPDF q; q.processFile(out.toUtf8().constData());
    QPDFAcroFormDocumentHelper af(q);
    REQUIRE(af.hasAcroForm());
    auto fields = af.getFormFields();
    REQUIRE(fields.size() == 2);
    std::set<std::string> names;
    for (auto& f : fields) { names.insert(f.getFullyQualifiedName()); CHECK(f.getValueAsString() == "Foxtrot"); }
    CHECK(names.size() == 2);          // conflict resolved by renaming
    CHECK(names.count("name") == 1);
    // Widgets are on pages 1 and 5 of the result.
    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    REQUIRE(pages.size() == 5);
    CHECK(af.getWidgetAnnotationsForPage(pages[0]).size() == 1);
    CHECK(af.getWidgetAnnotationsForPage(pages[4]).size() == 1);
    // Outline: one entry per input, the form's own "Section" bookmark nested and remapped.
    QPDFOutlineDocumentHelper od(q);
    auto top = od.getTopLevelOutlines();
    REQUIRE(top.size() == 3);
    CHECK(top[0].getTitle() == "form");
    CHECK(top[1].getTitle() == "three");
    REQUIRE(top[2].getKids().size() == 1);
    CHECK(top[2].getKids()[0].getTitle() == "Section");
    CHECK(top[2].getKids()[0].getDestPage().getObjGen() == pages[4].getObjectHandle().getObjGen());
    CHECK(top[1].getDestPage().getObjGen() == pages[1].getObjectHandle().getObjGen());
}
TEST_CASE("page ops read and write non-ASCII paths", "[pageops][unicode]") {
    auto out = tmpFile("rotated é ü 日本.pdf");
    REQUIRE(PageOps::rotate(path("unicode é.pdf"), {0}, 90, out).ok);
    CHECK(QFile::exists(out));
    CHECK(PageOps::rotations(out) == QList<int>{90, 0});
    CHECK(PageOps::pageCount(out) == 2);
}
