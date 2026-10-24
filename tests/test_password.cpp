#include <catch2/catch_test_macros.hpp>
#include "Document.h"
#include "PageOps.h"
#include "Fixtures.h"
#include <QFile>
using namespace pdfcore;

TEST_CASE("user-password files report NeedsPassword / WrongPassword", "[password]") {
    for (auto n : {"enc-aes256-user.pdf", "enc-aes128-user.pdf", "enc-rc4-user.pdf"}) {
        INFO(n);
        QString err; OpenError code = OpenError::None;
        CHECK_FALSE(Document::open(fixtures::path(n), QString(), &err, &code));
        CHECK(code == OpenError::NeedsPassword);
        CHECK_FALSE(Document::open(fixtures::path(n), "nope", &err, &code));
        CHECK(code == OpenError::WrongPassword);
        CHECK_FALSE(err.contains("nope")); // the password never appears in messages
        auto d = Document::open(fixtures::path(n), "user-pw", &err, &code);
        REQUIRE(d);
        CHECK(code == OpenError::None);
        CHECK(d->isEncrypted());
        CHECK(d->password() == "user-pw");
        CHECK(d->pageCount() == 3);
        CHECK_FALSE(d->render(0, 0.5).isNull());
        CHECK(d->pageText(1).contains("Page 2 Bravo"));
        // The owner password unlocks it as well.
        CHECK(Document::open(fixtures::path(n), "owner-pw", &err, &code));
    }
}

TEST_CASE("owner-password-only files open without a prompt", "[password]") {
    QString err; OpenError code = OpenError::Other;
    auto d = Document::open(fixtures::path("enc-aes256-owner-only.pdf"), QString(), &err, &code);
    REQUIRE(d);
    CHECK(code == OpenError::None);
    CHECK(d->password().isEmpty());
    CHECK(d->pageCount() == 3);
    // Page ops need no password either.
    const QString out = fixtures::tmpFile("owner-only-rot.pdf");
    auto r = PageOps::rotate(fixtures::path("enc-aes256-owner-only.pdf"), {0}, 90, out);
    REQUIRE(r.ok);
    CHECK(PageOps::rotations(out) == QList<int>{90, 0, 0});
}

TEST_CASE("page operations on an unlocked file keep its encryption", "[password][pageops]") {
    const QString in = fixtures::tmpFile("enc-work.pdf");
    QFile::remove(in);
    REQUIRE(QFile::copy(fixtures::path("enc-aes256-user.pdf"), in));
    // Without the password QPDF refuses the file.
    CHECK_FALSE(PageOps::rotate(in, {0}, 90, in).ok);
    {
        PageOps::ScopedPassword pw("user-pw");
        auto r = PageOps::rotate(in, {1}, 90, in); // in place, like the working copy
        REQUIRE(r.ok);
        CHECK(PageOps::rotations(in) == QList<int>{0, 90, 0});
        const QString ex = fixtures::tmpFile("enc-extract.pdf");
        REQUIRE(PageOps::extract(in, {2, 0}, ex).ok);
        CHECK(PageOps::pageCount(ex) == 2);
        CHECK(fixtures::isEncrypted(ex, "user-pw"));
        const QString plain = fixtures::tmpFile("enc-decrypted.pdf");
        REQUIRE(PageOps::copyDecrypted(in, plain).ok);
        OpenError code = OpenError::Other;
        auto p = Document::open(plain, QString(), nullptr, &code);
        REQUIRE(p);
        CHECK_FALSE(p->isEncrypted());
    }
    CHECK(fixtures::isEncrypted(in, "user-pw"));
    OpenError code = OpenError::None;
    CHECK_FALSE(Document::open(in, QString(), nullptr, &code));
    CHECK(code == OpenError::NeedsPassword);
    auto d = Document::open(in, "user-pw", nullptr, &code);
    REQUIRE(d);
    CHECK(d->pageCount() == 3);
}
