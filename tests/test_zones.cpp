#include <catch2/catch_test_macros.hpp>
#include "ZoneXmlIO.h"
#include "Fixtures.h"
#include <QFile>
#include <QProcess>
using namespace pdfcore;
using fixtures::tmpFile;

static const QString kSample = QStringLiteral(PDFMGR_SOURCE_DIR "/tests/zones/sample.zones.xml");

static QByteArray wrap(const QByteArray& body) {
    return "<?xml version=\"1.0\"?><pdfZones xmlns=\"urn:io-e:pdfmgr:zones:1\" version=\"1.0\">" + body + "</pdfZones>";
}
static QString readError(const QByteArray& xml) {
    ZoneXmlError e;
    auto d = readZonesXml(xml, &e);
    return d ? QString() : e.toString();
}

TEST_CASE("Zone XML sample is read with all attributes and OCR candidates", "[zones]") {
    ZoneXmlError e;
    auto doc = readZonesFile(kSample, &e);
    INFO(e.toString().toStdString());
    REQUIRE(doc);
    CHECK(doc->source == "manuel_partie_1.pdf");
    REQUIRE(doc->margins);
    CHECK(doc->margins->page == "*");
    CHECK(doc->margins->left == 51.0);
    CHECK(doc->margins->confidence == 0.95);
    REQUIRE(doc->pages.size() == 1);
    const ZonePage& p = doc->pages[0];
    CHECK(p.number == 3);
    CHECK(p.width == 595.28);
    REQUIRE(p.margins);
    CHECK(p.margins->origin == ZoneSource::Manual);
    REQUIRE(p.zones.size() == 5);
    CHECK(p.zones[0].type == ZoneType::Header);
    CHECK(p.zones[1].chapter == "Ch02");
    CHECK(p.zones[2].lang == "fra");
    CHECK(p.zones[2].resultsOfKind(OcrResult::Kind::Text).first().value == QString::fromUtf8("Soit f une fonction définie sur [a,b]..."));

    const Zone* f = doc->zone("p3-z4");
    REQUIRE(f);
    CHECK(f->type == ZoneType::Formula);
    CHECK(f->source == ZoneSource::Manual);
    CHECK(f->locked);
    CHECK_FALSE(f->confidence);
    auto latex = f->resultsOfKind(OcrResult::Kind::Latex);
    REQUIRE(latex.size() == 3);  // every engine candidate is kept
    CHECK(latex[0].engine == "pix2text-mfr");
    CHECK(latex[0].value == "\\int_0^1 x^2\\,dx = \\tfrac{1}{3}");
    CHECK(latex[1].engine == "unimernet");
    CHECK_FALSE(latex[2].confidence);
    CHECK(f->resultsOfKind(OcrResult::Kind::Alt).size() == 1);
    CHECK(doc->zone("p3-z5")->image == "Ch02/images/manuel_partie_1-image001.png");
}

TEST_CASE("Zone XML round-trips through write and read", "[zones]") {
    auto doc = readZonesFile(kSample);
    REQUIRE(doc);
    const QByteArray xml = writeZonesXml(*doc);
    ZoneXmlError e;
    auto again = readZonesXml(xml, &e);
    INFO(e.toString().toStdString());
    REQUIRE(again);
    CHECK(*again == *doc);
    CHECK(writeZonesXml(*again) == xml);  // stable serialization
}

TEST_CASE("Zone model built in code round-trips through a file", "[zones]") {
    ZoneDocument doc;
    doc.source = "a&b <c>.pdf";
    ZonePage p;
    p.number = 1; p.width = 612; p.height = 792; p.rotation = 90;
    Zone z;
    z.id = "p1-z1"; z.type = ZoneType::MarginNote; z.x = 0.1; z.y = 1e-3; z.w = 10.25; z.h = 3;
    z.order = 7; z.confidence = 0.5;
    z.results << OcrResult{OcrResult::Kind::Latex, "a", 0.1, "x < y && \\frac{1}{2}"}
              << OcrResult{OcrResult::Kind::Latex, "b", std::nullopt, "  spaced  "};
    p.zones << z;
    doc.pages << p;
    const QString path = tmpFile("roundtrip.zones.xml");
    QString err;
    REQUIRE(writeZonesFile(path, doc, &err));
    auto back = readZonesFile(path);
    REQUIRE(back);
    CHECK(*back == doc);
}

TEST_CASE("Zone XML reports structural errors", "[zones]") {
    CHECK(readError("<pdfZones version=\"1.0\"/>").contains("namespace"));
    CHECK(readError("<?xml version=\"1.0\"?><pdfZones xmlns=\"urn:io-e:pdfmgr:zones:1\"/>").contains("version"));
    CHECK(readError("<?xml version=\"1.0\"?><pdfZones xmlns=\"urn:io-e:pdfmgr:zones:1\" version=\"2.0\"/>").contains("unsupported version"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\"><zone id=\"a\" type=\"blob\" x=\"0\" y=\"0\" w=\"1\" h=\"1\" order=\"0\" source=\"auto\"/></page>")).contains("unknown type"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\"><zone id=\"a\" type=\"text\" x=\"0\" y=\"0\" w=\"1\" order=\"0\" source=\"auto\"/></page>")).contains("'h'"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\"><zone id=\"a\" type=\"text\" x=\"abc\" y=\"0\" w=\"1\" h=\"1\" order=\"0\" source=\"auto\"/></page>")).contains("not a number"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\"><zone id=\"a\" type=\"text\" x=\"0\" y=\"0\" w=\"1\" h=\"1\" order=\"0\" source=\"robot\"/></page>")).contains("'auto' or 'manual'"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\"><zone id=\"a\" type=\"text\" x=\"0\" y=\"0\" w=\"1\" h=\"1\" order=\"0\" source=\"auto\" colour=\"red\"/></page>")).contains("unknown attribute 'colour'"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\"><zone id=\"a\" type=\"text\" x=\"0\" y=\"0\" w=\"1\" h=\"1\" order=\"0\" source=\"auto\"><latex>x</latex></zone></page>")).contains("'engine'"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\"><zone id=\"a\" type=\"text\" x=\"0\" y=\"0\" w=\"1\" h=\"1\" order=\"0\" source=\"auto\"><mathml engine=\"e\"/></zone></page>")).contains("unexpected element <mathml>"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\"><zone id=\"a\" type=\"text\" x=\"0\" y=\"0\" w=\"1\" h=\"1\" order=\"0\" source=\"auto\" locked=\"maybe\"/></page>")).contains("boolean"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\"><zone id=\"a\"")).size() > 0);  // malformed XML
    // Line numbers are reported.
    ZoneXmlError e;
    REQUIRE_FALSE(readZonesXml("<?xml version=\"1.0\"?>\n<pdfZones xmlns=\"urn:io-e:pdfmgr:zones:1\" version=\"1.0\">\n<bogus/>\n</pdfZones>", &e));
    CHECK(e.line == 3);
}

TEST_CASE("Zone XML reports semantic errors", "[zones]") {
    const QByteArray z = "<zone id=\"dup\" type=\"text\" x=\"0\" y=\"0\" w=\"1\" h=\"1\" order=\"0\" source=\"auto\"/>";
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\">" + z + z + "</page>")).contains("duplicate zone id"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\"/><page number=\"1\" width=\"1\" height=\"1\"/>")).contains("duplicate page"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\"><zone id=\"a\" type=\"text\" x=\"0\" y=\"0\" w=\"-1\" h=\"1\" order=\"0\" source=\"auto\"/></page>")).contains("negative width"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\"><zone id=\"a\" type=\"text\" x=\"0\" y=\"0\" w=\"1\" h=\"1\" order=\"0\" source=\"auto\"><latex engine=\"e\" confidence=\"1.5\">x</latex></zone></page>")).contains("confidence"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\" rotation=\"45\"/>")).contains("rotation"));
    CHECK(readError(wrap("<page number=\"1\" width=\"1\" height=\"1\"/><margins page=\"*\" top=\"1\" bottom=\"1\" left=\"1\" right=\"1\"/>")).contains("before any <page>"));

    ZoneDocument bad;
    bad.pages << ZonePage{};
    bad.pages[0].zones << Zone{} << Zone{};
    bad.pages[0].zones[0].id = bad.pages[0].zones[1].id = "x";
    QString err;
    CHECK_FALSE(writeZonesFile(tmpFile("bad.zones.xml"), bad, &err));
    CHECK(err.contains("duplicate zone id"));
}

TEST_CASE("Zone type names round-trip", "[zones]") {
    const QStringList names = allZoneTypeNames();
    CHECK(names.size() == 13);
    for (const auto& n : names) {
        auto t = zoneTypeFromString(n);
        REQUIRE(t);
        CHECK(zoneTypeToString(*t) == n);
    }
    CHECK_FALSE(zoneTypeFromString("Text"));
}

#ifdef PDFMGR_XMLLINT
TEST_CASE("Written zone XML validates against the XSD", "[zones][xsd]") {
    auto doc = readZonesFile(kSample);
    REQUIRE(doc);
    const QString path = tmpFile("xsd.zones.xml");
    REQUIRE(writeZonesFile(path, *doc));
    QProcess p;
    p.start(PDFMGR_XMLLINT, {"--noout", "--schema", PDFMGR_SOURCE_DIR "/schemas/pdfmgr-zones.xsd", path});
    REQUIRE(p.waitForFinished(30000));
    INFO(p.readAllStandardError().toStdString());
    CHECK(p.exitCode() == 0);
}
#endif
