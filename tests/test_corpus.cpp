// Data-driven: every corpus file is opened, rendered, rotated and saved.
#include <catch2/catch_test_macros.hpp>
#include "Corpus.h"
#include "Document.h"
#include "PageOps.h"
#include "Fixtures.h"
#include <QFile>
#include <QMap>
#include <algorithm>
using namespace pdfcore;

namespace {
// Zoom so that the longest side is about `px` pixels (keeps huge pages cheap).
double fitZoom(const Document& d, int page, int px = 400) {
    QSizeF s = d.pageSize(page);
    return px / std::max({s.width(), s.height(), 1.0});
}
}

TEST_CASE("corpus has 50 files in the planned categories", "[corpus]") {
    const auto all = corpus::entries();
    CHECK(all.size() == 50);
    QMap<QString, int> n;
    for (const auto& e : all) { ++n[e.category]; CHECK(QFile::exists(corpus::path(e.name))); }
    CHECK(n["base"] == 8);
    CHECK(n["forms"] == 5); CHECK(n["scanned"] == 5); CHECK(n["encrypted"] == 5); CHECK(n["large"] == 5);
    CHECK(n["unicode"] == 5); CHECK(n["sizes"] == 4); CHECK(n["malformed"] == 4); CHECK(n["rotation"] == 4);
    CHECK(n["linearized"] == 3); CHECK(n["minimal"] == 2);
}

TEST_CASE("corpus: open, render, rotate, save", "[corpus]") {
    for (const auto& e : corpus::entries()) {
        const QString f = corpus::path(e.name);
        INFO(e.category.toStdString() << ": " << e.name.toStdString());
        QString err; OpenError code = OpenError::None;
        if (e.garbage) { CHECK_FALSE(Document::open(f, QString(), &err, &code)); continue; }
        if (!e.password.isEmpty()) {
            CHECK_FALSE(Document::open(f, QString(), &err, &code));
            CHECK(code == OpenError::NeedsPassword);
        }
        auto d = Document::open(f, e.password, &err, &code);
        INFO(err.toStdString());
        REQUIRE(d);
        CHECK(d->pageCount() == e.pages);
        for (int p : {0, d->pageCount() / 2, d->pageCount() - 1}) {
            QImage img = d->render(p, fitZoom(*d, p));
            CHECK_FALSE(img.isNull());
        }
        d.reset();

        PageOps::ScopedPassword pw(e.password);
        QString rerr;
        QList<int> before = PageOps::rotations(f, &rerr);
        INFO(rerr.toStdString());
        REQUIRE(before.size() == e.pages);
        if (!e.rotate.isEmpty()) CHECK(before == e.rotate);

        const QString rotated = fixtures::tmpFile("corpus-rotated.pdf");
        auto r = PageOps::rotate(f, {0}, 90, rotated);
        INFO(r.error.toStdString());
        REQUIRE(r.ok);
        auto after = PageOps::rotations(rotated);
        REQUIRE(after.size() == e.pages);
        CHECK(after[0] == (before[0] + 90) % 360);

        const QString saved = fixtures::tmpFile("corpus-saved.pdf");
        REQUIRE(PageOps::copy(rotated, saved).ok);
        auto d2 = Document::open(saved, e.password, &err, &code);
        REQUIRE(d2);
        CHECK(d2->pageCount() == e.pages);
        CHECK_FALSE(d2->render(0, fitZoom(*d2, 0)).isNull());
        if (e.category == "encrypted" && !e.password.isEmpty()) CHECK(d2->isEncrypted());
    }
}
