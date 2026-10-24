#include <catch2/catch_test_macros.hpp>
#include "TileCache.h"
#include "Document.h"
#include "PageOps.h"
#include "CancelToken.h"
#include "Fixtures.h"
#include <cstdlib>
using namespace pdfcore;

static QImage tile(int w = 256, int h = 256) { QImage i(w, h, QImage::Format_RGB888); i.fill(Qt::white); return i; }
static TileKey key(quint64 gen, int page, int tx, int ty = 0, double zoom = 1.0) { return {gen, page, TileCache::zoomKey(zoom), tx, ty}; }

TEST_CASE("tile cache: LRU eviction bounded by bytes", "[tiles]") {
    const qint64 one = TileCache::cost(tile());
    TileCache c(5 * one);
    for (int i = 0; i < 10; ++i) {
        c.insert(key(1, 0, i), tile());
        CHECK(c.bytes() <= c.maxBytes());
        if (i == 3) CHECK_FALSE(c.get(key(1, 0, 0)).isNull()); // touch tile 0: becomes most recent
    }
    CHECK(c.count() == 5);
    CHECK(c.bytes() == 5 * one);
    CHECK(c.evictions() == 5);
    CHECK(c.contains(key(1, 0, 9)));
    CHECK_FALSE(c.contains(key(1, 0, 1)));   // least recently used went first
    // Tile 0 was touched after tiles 1..3, so it survived longer than them, but
    // tiles 5..9 are newer still.
    CHECK_FALSE(c.contains(key(1, 0, 4)));
    c.insert(key(1, 0, 9), tile());          // replace: no double counting
    CHECK(c.bytes() == 5 * one);
    c.insert(key(1, 0, 99), tile(4096, 4096)); // larger than the whole cache: dropped
    CHECK_FALSE(c.contains(key(1, 0, 99)));
    CHECK(c.count() == 5);
    c.setMaxBytes(2 * one);
    CHECK(c.count() == 2);
    CHECK(c.bytes() <= 2 * one);
}

TEST_CASE("tile cache: invalidation by document generation and page", "[tiles]") {
    TileCache c(64ll << 20);
    for (int p = 0; p < 3; ++p) for (int t = 0; t < 4; ++t) { c.insert(key(1, p, t), tile()); c.insert(key(2, p, t), tile()); }
    CHECK(c.count() == 24);
    c.invalidate(1);
    CHECK(c.count() == 12);
    CHECK_FALSE(c.contains(key(1, 0, 0)));
    CHECK(c.contains(key(2, 0, 0)));
    c.invalidatePage(2, 1);
    CHECK(c.count() == 8);
    CHECK_FALSE(c.contains(key(2, 1, 3)));
    CHECK(c.bytes() == 8 * TileCache::cost(tile()));
    c.clear();
    CHECK(c.count() == 0); CHECK(c.bytes() == 0);
}

TEST_CASE("tile geometry", "[tiles]") {
    const QSize page(600, 300);
    CHECK(Tiles::rect(0, 0, page, 256) == QRect(0, 0, 256, 256));
    CHECK(Tiles::rect(2, 1, page, 256) == QRect(512, 256, 88, 44)); // clipped last column/row
    CHECK(Tiles::visible(page, QRect(0, 0, 600, 300), 256).size() == 6);
    CHECK(Tiles::visible(page, QRect(300, 10, 10, 10), 256) == QList<QPoint>{QPoint(1, 0)});
    CHECK(Tiles::visible(page, QRect(700, 0, 10, 10), 256).isEmpty());
}

TEST_CASE("tile planner: scrolling back issues no render jobs", "[tiles]") {
    TileCache c(64ll << 20);
    TilePlanner pl(&c);
    const QSize page(1190, 1684);
    const QRect top(0, 0, 800, 600), bottom(0, 1000, 800, 600);
    auto a = pl.plan(1, 0, 2.0, page, top);
    CHECK(a.size() == 12); // 4 columns x 3 rows
    CHECK(pl.plan(1, 0, 2.0, page, top).isEmpty()); // already in flight
    for (const auto& k : a) pl.done(k, tile(), 1);
    CHECK(pl.inFlight() == 0);
    auto b = pl.plan(1, 0, 2.0, page, bottom);
    CHECK_FALSE(b.isEmpty());
    // Tiles of the old view that are still running are reported as stale.
    CHECK(pl.stale({}).size() == b.size());
    for (const auto& k : b) pl.done(k, tile(), 1);
    const quint64 before = pl.submitted();
    CHECK(pl.plan(1, 0, 2.0, page, top).isEmpty());   // scroll back: all cached
    CHECK(pl.plan(1, 0, 2.0, page, bottom).isEmpty());
    CHECK(pl.submitted() == before);
    CHECK_FALSE(pl.plan(1, 0, 2.5, page, top).isEmpty()); // new zoom: new tiles
}

TEST_CASE("tile rendering matches the full-page render", "[tiles][document]") {
    auto d = Document::open(fixtures::path("three.pdf"));
    REQUIRE(d);
    const double z = 1.5;
    QImage full = d->render(0, z);
    REQUIRE(d->pageSizePx(0, z) == full.size());
    const QRect r = Tiles::rect(0, 0, full.size(), 256);
    QImage t = d->renderTile(0, z, r);
    REQUIRE(t.size() == r.size());
    QImage ref = full.copy(r);
    long diff = 0;
    for (int y = 0; y < t.height(); ++y) for (int x = 0; x < t.width(); ++x)
        diff += std::abs(qGray(t.pixel(x, y)) - qGray(ref.pixel(x, y)));
    CHECK(diff / (t.width() * t.height()) <= 2);
    // The last (partial) tile.
    const QRect last = Tiles::rect((full.width() - 1) / 256, (full.height() - 1) / 256, full.size(), 256);
    CHECK(d->renderTile(0, z, last).size() == last.size());
    CancelToken c; c.cancel();
    CHECK(d->renderTile(0, z, r, &c).isNull());
}

TEST_CASE("tile cache stays under its cap on a 2000-page file", "[tiles][large]") {
    const QString f = fixtures::tmpFile("tiles-2000.pdf");
    fixtures::writePdf(f, 2000, "Tile");
    auto d = Document::open(f);
    REQUIRE(d);
    REQUIRE(d->pageCount() == 2000);
    TileCache c(8ll << 20); // 8 MB
    TilePlanner pl(&c);
    const double z = 1.0;
    const QSize page = d->pageSizePx(0, z);
    for (int p = 0; p < 2000; p += 7) { // scroll through the whole file
        for (const auto& k : pl.plan(1, p, z, page, QRect(0, 0, page.width(), 400))) {
            pl.done(k, d->renderTile(p, z, Tiles::rect(k.tx, k.ty, page)), 1);
            REQUIRE(c.bytes() <= c.maxBytes());
        }
    }
    CHECK(c.evictions() > 0);
    CHECK(c.bytes() <= 8ll << 20);
}

TEST_CASE("no stale tile after a rotate", "[tiles]") {
    TileCache c(64ll << 20);
    TilePlanner pl(&c);
    const QString in = fixtures::tmpFile("tiles-rot-in.pdf"), out = fixtures::tmpFile("tiles-rot-out.pdf");
    fixtures::writePdf(in, 2, "Rot");
    auto d1 = Document::open(in);
    const QSize s1 = d1->pageSizePx(0, 1.0);
    auto k1 = pl.plan(1, 0, 1.0, s1, QRect(QPoint(0, 0), s1));
    // A job of generation 1 is still running when the working copy changes.
    const TileKey late = k1.takeLast();
    for (const auto& k : k1) pl.done(k, d1->renderTile(0, 1.0, Tiles::rect(k.tx, k.ty, s1)), 1);
    REQUIRE(PageOps::rotate(in, {0}, 90, out).ok);
    // applyWorkingCopy: new document generation, old tiles invalidated.
    c.invalidate(1);
    auto d2 = Document::open(out);
    const QSize s2 = d2->pageSizePx(0, 1.0);
    CHECK(s2 == s1.transposed());
    pl.done(late, tile(), 2); // stale result arrives: not cached
    CHECK(c.count() == 0);
    auto k2 = pl.plan(2, 0, 1.0, s2, QRect(QPoint(0, 0), s2));
    CHECK(k2.size() == Tiles::visible(s2, QRect(QPoint(0, 0), s2)).size()); // everything re-rendered
    for (const auto& k : k2) pl.done(k, d2->renderTile(0, 1.0, Tiles::rect(k.tx, k.ty, s2)), 2);
    QImage t = c.get(k2.first());
    REQUIRE_FALSE(t.isNull());
    CHECK(t.size() == Tiles::rect(0, 0, s2).size());
}
