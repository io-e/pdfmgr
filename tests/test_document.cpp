#include <catch2/catch_test_macros.hpp>
#include "Document.h"
#include "Fixtures.h"
#include <thread>
#include <vector>
using namespace pdfcore;

TEST_CASE("open corpus files", "[document]") {
    for (auto n : {"one.pdf", "three.pdf", "five.pdf", "landscape.pdf", "unicode é.pdf", "objstm.pdf"}) {
        QString err;
        auto d = Document::open(fixtures::path(n), &err);
        INFO(n << " " << err.toStdString());
        REQUIRE(d);
        REQUIRE(d->pageCount() > 0);
    }
    REQUIRE(Document::open(fixtures::path("three.pdf"))->pageCount() == 3);
}
TEST_CASE("garbage and missing files fail cleanly", "[document]") {
    QString err;
    CHECK_FALSE(Document::open(fixtures::path("garbage.pdf"), &err));
    CHECK_FALSE(Document::open(fixtures::path("nope.pdf"), &err));
    CHECK_FALSE(err.isEmpty());
}
TEST_CASE("render and thumbnail", "[document]") {
    auto d = Document::open(fixtures::path("three.pdf"));
    QSizeF s = d->pageSize(0);
    CHECK(int(s.width()) == 595); CHECK(int(s.height()) == 842);
    QImage img = d->render(0, 2.0);
    REQUIRE_FALSE(img.isNull());
    CHECK(img.width() == 1190); CHECK(img.height() == 1684);
    bool hasDark = false;
    for (int y = 100; y < 300 && !hasDark; ++y) for (int x = 100; x < 800; ++x) if (qGray(img.pixel(x, y)) < 100) { hasDark = true; break; }
    CHECK(hasDark); // text was drawn
    QImage t = d->thumbnail(0, 160);
    CHECK(std::max(t.width(), t.height()) == 160);
    CHECK(d->render(99, 1.0).isNull());
    CancelToken abort; abort.cancel();
    CHECK(d->render(0, 1.0, &abort).isNull()); // pre-aborted via fz_cookie
}
TEST_CASE("text extraction and search", "[document]") {
    auto d = Document::open(fixtures::path("five.pdf"));
    CHECK(d->pageText(2).contains("Page 3 Charlie"));
    auto hits = d->search("Charlie");
    CHECK(hits.size() == 5);
    auto h3 = d->search("Page 4");
    REQUIRE(h3.size() == 1);
    CHECK(h3[0].page == 3);
    CHECK(h3[0].rect.top() < 200); // near the top of the page (MuPDF y-down coords)
    CHECK(d->search("Zulu").isEmpty());
}
TEST_CASE("concurrent rendering with cloned contexts", "[document][threads]") {
    auto d = Document::open(fixtures::path("five.pdf"));
    std::vector<std::thread> ts; std::atomic<int> ok{0};
    for (int i = 0; i < 8; ++i) ts.emplace_back([&, i] { for (int k = 0; k < 5; ++k) if (!d->render((i + k) % 5, 0.5).isNull()) ++ok; });
    for (auto& t : ts) t.join();
    CHECK(ok == 40);
}
