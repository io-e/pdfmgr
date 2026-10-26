#include "ZoneXmlIO.h"
#include "AtomicWrite.h"
#include <QFile>
#include <QLocale>
#include <QSet>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace pdfcore {

QString ZoneXmlError::toString() const {
    return line > 0 ? QString("line %1, column %2: %3").arg(line).arg(column).arg(message) : message;
}

namespace {

QString num(double d) { return QString::number(d, 'g', QLocale::FloatingPointShortest); }

struct ParseFail {
    QString msg;
};

class Parser {
public:
    explicit Parser(const QByteArray& xml) : r_(xml) {}

    ZoneDocument run() {
        while (!r_.atEnd() && !r_.isStartElement()) r_.readNext();
        if (r_.hasError()) throw ParseFail{r_.errorString()};
        expectElement("pdfZones");
        ZoneDocument doc;
        allowOnly({"version", "source", "sourceSha256", "unit", "generator", "created"});
        doc.version = required("version");
        if (doc.version != QLatin1String(kZonesVersion))
            throw ParseFail{QString("unsupported version '%1' (expected %2)").arg(doc.version, kZonesVersion)};
        doc.source = attr("source");
        doc.sourceSha256 = attr("sourceSha256");
        doc.generator = attr("generator");
        doc.created = attr("created");
        if (has("unit")) doc.unit = attr("unit");
        if (doc.unit != "pt") throw ParseFail{QString("unsupported unit '%1' (only 'pt')").arg(doc.unit)};

        bool seenPage = false;
        while (nextChild()) {
            if (r_.name() == u"margins") {
                if (seenPage || doc.margins) throw ParseFail{"global <margins> must appear once, before any <page>"};
                doc.margins = readMargins(true);
            } else if (r_.name() == u"page") {
                seenPage = true;
                doc.pages << readPage();
            } else {
                unexpected();
            }
        }
        while (!r_.atEnd()) r_.readNext();
        if (r_.hasError()) throw ParseFail{r_.errorString()};
        return doc;
    }

    QXmlStreamReader r_;

private:
    void expectElement(const char* name) {
        if (r_.name() != QLatin1String(name) || r_.namespaceUri() != QLatin1String(kZonesNamespace))
            throw ParseFail{QString("expected <%1> in namespace %2, found <%3> in '%4'")
                                .arg(name, kZonesNamespace, r_.name().toString(), r_.namespaceUri().toString())};
    }
    // Advances to the next child start element of the current element; false at its end.
    bool nextChild() {
        while (r_.readNextStartElement()) {
            if (r_.namespaceUri() != QLatin1String(kZonesNamespace))
                throw ParseFail{QString("element <%1> is not in namespace %2").arg(r_.name().toString(), kZonesNamespace)};
            return true;
        }
        if (r_.hasError()) throw ParseFail{r_.errorString()};
        return false;
    }
    [[noreturn]] void unexpected() {
        throw ParseFail{QString("unexpected element <%1>").arg(r_.name().toString())};
    }
    void allowOnly(std::initializer_list<const char*> names) {
        for (const auto& a : r_.attributes()) {
            if (!a.namespaceUri().isEmpty()) continue;  // foreign-namespace attributes are allowed
            bool ok = false;
            for (auto n : names) ok = ok || a.name() == QLatin1String(n);
            if (!ok)
                throw ParseFail{QString("unknown attribute '%1' on <%2>").arg(a.name().toString(), r_.name().toString())};
        }
    }
    bool has(const char* n) const { return r_.attributes().hasAttribute(QLatin1String(n)); }
    QString attr(const char* n) const { return r_.attributes().value(QLatin1String(n)).toString(); }
    QString required(const char* n) const {
        if (!has(n)) throw ParseFail{QString("<%1> is missing required attribute '%2'").arg(r_.name().toString(), n)};
        return attr(n);
    }
    double dbl(const char* n, bool req) const {
        if (!req && !has(n)) return 0;
        bool ok = false;
        double v = required(n).trimmed().toDouble(&ok);
        if (!ok) throw ParseFail{QString("attribute '%1' is not a number: '%2'").arg(n, attr(n))};
        return v;
    }
    int integer(const char* n, bool req) const {
        if (!req && !has(n)) return 0;
        bool ok = false;
        int v = required(n).trimmed().toInt(&ok);
        if (!ok) throw ParseFail{QString("attribute '%1' is not an integer: '%2'").arg(n, attr(n))};
        return v;
    }
    std::optional<double> optDbl(const char* n) const {
        if (!has(n)) return std::nullopt;
        return dbl(n, true);
    }
    ZoneSource source(const char* n) const {
        if (!has(n)) return ZoneSource::Auto;
        auto s = zoneSourceFromString(attr(n));
        if (!s) throw ParseFail{QString("attribute '%1' must be 'auto' or 'manual', not '%2'").arg(n, attr(n))};
        return *s;
    }
    void noChildren() {
        if (nextChild()) unexpected();
    }

    Margins readMargins(bool global) {
        if (global) allowOnly({"page", "top", "bottom", "left", "right", "origin", "confidence"});
        else allowOnly({"top", "bottom", "left", "right", "origin", "confidence"});
        Margins m;
        if (global) m.page = required("page");
        m.top = dbl("top", true);
        m.bottom = dbl("bottom", true);
        m.left = dbl("left", true);
        m.right = dbl("right", true);
        m.origin = source("origin");
        m.confidence = optDbl("confidence");
        noChildren();
        return m;
    }

    ZonePage readPage() {
        allowOnly({"number", "width", "height", "rotation"});
        ZonePage p;
        p.number = integer("number", true);
        p.width = dbl("width", true);
        p.height = dbl("height", true);
        p.rotation = integer("rotation", false);
        bool seenZone = false;
        while (nextChild()) {
            if (r_.name() == u"margins") {
                if (seenZone || p.margins) throw ParseFail{"page <margins> must appear once, before any <zone>"};
                p.margins = readMargins(false);
            } else if (r_.name() == u"zone") {
                seenZone = true;
                p.zones << readZone();
            } else {
                unexpected();
            }
        }
        return p;
    }

    Zone readZone() {
        allowOnly({"id", "type", "x", "y", "w", "h", "order", "source", "locked", "confidence", "lang", "chapter", "image"});
        Zone z;
        z.id = required("id");
        const QString t = required("type");
        auto type = zoneTypeFromString(t);
        if (!type) throw ParseFail{QString("zone '%1': unknown type '%2'").arg(z.id, t)};
        z.type = *type;
        z.x = dbl("x", true);
        z.y = dbl("y", true);
        z.w = dbl("w", true);
        z.h = dbl("h", true);
        z.order = integer("order", true);
        required("source");
        z.source = source("source");
        if (has("locked")) {
            const QString l = attr("locked");
            if (l == "true" || l == "1") z.locked = true;
            else if (l == "false" || l == "0") z.locked = false;
            else throw ParseFail{QString("zone '%1': locked must be a boolean, not '%2'").arg(z.id, l)};
        }
        z.confidence = optDbl("confidence");
        z.lang = attr("lang");
        z.chapter = attr("chapter");
        z.image = attr("image");
        while (nextChild()) {
            OcrResult res;
            if (r_.name() == u"text") res.kind = OcrResult::Kind::Text;
            else if (r_.name() == u"latex") res.kind = OcrResult::Kind::Latex;
            else if (r_.name() == u"alt") res.kind = OcrResult::Kind::Alt;
            else unexpected();
            allowOnly({"engine", "confidence"});
            res.engine = required("engine");
            res.confidence = optDbl("confidence");
            res.value = r_.readElementText(QXmlStreamReader::ErrorOnUnexpectedElement);
            if (r_.hasError()) throw ParseFail{QString("zone '%1': %2").arg(z.id, r_.errorString())};
            z.results << res;
        }
        return z;
    }
};

void writeMargins(QXmlStreamWriter& w, const Margins& m, bool global) {
    w.writeEmptyElement("margins");
    if (global) w.writeAttribute("page", m.page.isEmpty() ? "*" : m.page);
    w.writeAttribute("top", num(m.top));
    w.writeAttribute("bottom", num(m.bottom));
    w.writeAttribute("left", num(m.left));
    w.writeAttribute("right", num(m.right));
    w.writeAttribute("origin", zoneSourceToString(m.origin));
    if (m.confidence) w.writeAttribute("confidence", num(*m.confidence));
}

}  // namespace

std::optional<ZoneDocument> readZonesXml(const QByteArray& xml, ZoneXmlError* error) {
    Parser p(xml);
    try {
        ZoneDocument doc = p.run();
        const QStringList errs = validateZones(doc);
        if (!errs.isEmpty()) {
            if (error) *error = {errs.join("; "), 0, 0};
            return std::nullopt;
        }
        return doc;
    } catch (const ParseFail& f) {
        if (error) *error = {f.msg, p.r_.lineNumber(), p.r_.columnNumber()};
        return std::nullopt;
    }
}

std::optional<ZoneDocument> readZonesFile(const QString& path, ZoneXmlError* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = {QString("cannot open %1: %2").arg(path, f.errorString()), 0, 0};
        return std::nullopt;
    }
    return readZonesXml(f.readAll(), error);
}

QByteArray writeZonesXml(const ZoneDocument& doc) {
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(2);
    w.writeStartDocument();
    w.writeDefaultNamespace(kZonesNamespace);
    w.writeStartElement("pdfZones");
    w.writeAttribute("version", doc.version.isEmpty() ? QString(kZonesVersion) : doc.version);
    if (!doc.source.isEmpty()) w.writeAttribute("source", doc.source);
    if (!doc.sourceSha256.isEmpty()) w.writeAttribute("sourceSha256", doc.sourceSha256);
    w.writeAttribute("unit", doc.unit);
    if (!doc.generator.isEmpty()) w.writeAttribute("generator", doc.generator);
    if (!doc.created.isEmpty()) w.writeAttribute("created", doc.created);
    if (doc.margins) writeMargins(w, *doc.margins, true);
    for (const auto& p : doc.pages) {
        w.writeStartElement("page");
        w.writeAttribute("number", QString::number(p.number));
        w.writeAttribute("width", num(p.width));
        w.writeAttribute("height", num(p.height));
        w.writeAttribute("rotation", QString::number(p.rotation));
        if (p.margins) writeMargins(w, *p.margins, false);
        for (const auto& z : p.zones) {
            w.writeStartElement("zone");
            w.writeAttribute("id", z.id);
            w.writeAttribute("type", zoneTypeToString(z.type));
            w.writeAttribute("x", num(z.x));
            w.writeAttribute("y", num(z.y));
            w.writeAttribute("w", num(z.w));
            w.writeAttribute("h", num(z.h));
            w.writeAttribute("order", QString::number(z.order));
            w.writeAttribute("source", zoneSourceToString(z.source));
            if (z.locked) w.writeAttribute("locked", "true");
            if (z.confidence) w.writeAttribute("confidence", num(*z.confidence));
            if (!z.lang.isEmpty()) w.writeAttribute("lang", z.lang);
            if (!z.chapter.isEmpty()) w.writeAttribute("chapter", z.chapter);
            if (!z.image.isEmpty()) w.writeAttribute("image", z.image);
            for (const auto& r : z.results) {
                w.writeStartElement(ocrKindToString(r.kind));
                w.writeAttribute("engine", r.engine);
                if (r.confidence) w.writeAttribute("confidence", num(*r.confidence));
                w.writeCharacters(r.value);
                w.writeEndElement();
            }
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    w.writeEndElement();
    w.writeEndDocument();
    return out;
}

bool writeZonesFile(const QString& path, const ZoneDocument& doc, QString* error) {
    const QStringList errs = validateZones(doc);
    if (!errs.isEmpty()) {
        if (error) *error = "invalid zone document: " + errs.join("; ");
        return false;
    }
    return atomicWrite(path, writeZonesXml(doc), error);
}

}  // namespace pdfcore
