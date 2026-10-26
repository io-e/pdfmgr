#include "ZoneModel.h"
#include <QSet>
#include <array>
#include <utility>

namespace pdfcore {
namespace {
constexpr std::array<std::pair<ZoneType, const char*>, 13> kTypes{{
    {ZoneType::Text, "text"}, {ZoneType::Title, "title"}, {ZoneType::Header, "header"},
    {ZoneType::Footer, "footer"}, {ZoneType::PageNumber, "page-number"},
    {ZoneType::Formula, "formula"}, {ZoneType::Figure, "figure"}, {ZoneType::Table, "table"},
    {ZoneType::Caption, "caption"}, {ZoneType::List, "list"}, {ZoneType::Footnote, "footnote"},
    {ZoneType::MarginNote, "margin-note"}, {ZoneType::Ignore, "ignore"},
}};
bool badConfidence(const std::optional<double>& c) { return c && (*c < 0.0 || *c > 1.0); }
}  // namespace

QString zoneTypeToString(ZoneType t) {
    for (const auto& [k, n] : kTypes)
        if (k == t) return QString::fromLatin1(n);
    return {};
}
std::optional<ZoneType> zoneTypeFromString(const QString& s) {
    for (const auto& [k, n] : kTypes)
        if (s == QLatin1String(n)) return k;
    return std::nullopt;
}
QStringList allZoneTypeNames() {
    QStringList l;
    for (const auto& [k, n] : kTypes) l << QString::fromLatin1(n);
    return l;
}
QString zoneSourceToString(ZoneSource s) { return s == ZoneSource::Manual ? "manual" : "auto"; }
std::optional<ZoneSource> zoneSourceFromString(const QString& s) {
    if (s == "auto") return ZoneSource::Auto;
    if (s == "manual") return ZoneSource::Manual;
    return std::nullopt;
}
QString ocrKindToString(OcrResult::Kind k) {
    switch (k) {
    case OcrResult::Kind::Text: return "text";
    case OcrResult::Kind::Latex: return "latex";
    case OcrResult::Kind::Alt: return "alt";
    }
    return {};
}

QList<OcrResult> Zone::resultsOfKind(OcrResult::Kind k) const {
    QList<OcrResult> out;
    for (const auto& r : results)
        if (r.kind == k) out << r;
    return out;
}

const ZonePage* ZoneDocument::page(int number) const {
    for (const auto& p : pages)
        if (p.number == number) return &p;
    return nullptr;
}
const Zone* ZoneDocument::zone(const QString& id) const {
    for (const auto& p : pages)
        for (const auto& z : p.zones)
            if (z.id == id) return &z;
    return nullptr;
}

QStringList validateZones(const ZoneDocument& doc) {
    QStringList errs;
    if (doc.unit != "pt") errs << QString("unsupported unit '%1' (only 'pt')").arg(doc.unit);
    auto checkMargins = [&](const Margins& m, const QString& where) {
        if (m.top < 0 || m.bottom < 0 || m.left < 0 || m.right < 0)
            errs << QString("%1: negative margin").arg(where);
        if (badConfidence(m.confidence)) errs << QString("%1: confidence out of [0,1]").arg(where);
    };
    if (doc.margins) checkMargins(*doc.margins, "global margins");
    QSet<int> pageNums;
    QSet<QString> ids;
    for (const auto& p : doc.pages) {
        const QString pw = QString("page %1").arg(p.number);
        if (p.number < 1) errs << QString("%1: page number must be >= 1").arg(pw);
        if (pageNums.contains(p.number)) errs << QString("%1: duplicate page number").arg(pw);
        pageNums.insert(p.number);
        if (p.width < 0 || p.height < 0) errs << QString("%1: negative page size").arg(pw);
        if (p.rotation % 90 != 0 || p.rotation < 0 || p.rotation > 270)
            errs << QString("%1: rotation must be 0, 90, 180 or 270").arg(pw);
        if (p.margins) checkMargins(*p.margins, pw + " margins");
        for (const auto& z : p.zones) {
            const QString zw = QString("zone '%1'").arg(z.id);
            if (z.id.isEmpty()) errs << QString("%1: zone without id").arg(pw);
            else if (ids.contains(z.id)) errs << QString("%1: duplicate zone id").arg(zw);
            ids.insert(z.id);
            if (z.w < 0 || z.h < 0) errs << QString("%1: negative width or height").arg(zw);
            if (z.order < 0) errs << QString("%1: negative order").arg(zw);
            if (badConfidence(z.confidence)) errs << QString("%1: confidence out of [0,1]").arg(zw);
            for (const auto& r : z.results) {
                if (r.engine.isEmpty())
                    errs << QString("%1: <%2> without engine").arg(zw, ocrKindToString(r.kind));
                if (badConfidence(r.confidence))
                    errs << QString("%1: <%2 engine=\"%3\"> confidence out of [0,1]")
                                .arg(zw, ocrKindToString(r.kind), r.engine);
            }
        }
    }
    return errs;
}

}  // namespace pdfcore
