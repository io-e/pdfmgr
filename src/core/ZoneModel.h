#pragma once
// Zone model for the zone-based OCR extension (spec A1, section 3.1).
// In-memory form of the operational zone XML (namespace urn:io-e:pdfmgr:zones:1).
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace pdfcore {

inline constexpr const char* kZonesNamespace = "urn:io-e:pdfmgr:zones:1";
inline constexpr const char* kZonesVersion = "1.0";

// Zone types (spec 3.1.1).
enum class ZoneType {
    Text, Title, Header, Footer, PageNumber, Formula, Figure, Table,
    Caption, List, Footnote, MarginNote, Ignore
};
QString zoneTypeToString(ZoneType t);
std::optional<ZoneType> zoneTypeFromString(const QString& s);
QStringList allZoneTypeNames();

enum class ZoneSource { Auto, Manual };
QString zoneSourceToString(ZoneSource s);
std::optional<ZoneSource> zoneSourceFromString(const QString& s);

// One OCR result child of a zone (spec 3.1.3). A zone keeps every candidate
// from every engine, e.g. several <latex> elements from different engines.
struct OcrResult {
    enum class Kind { Text, Latex, Alt };
    Kind kind = Kind::Text;
    QString engine;                    // required
    std::optional<double> confidence;  // 0.0-1.0
    QString value;                     // element content, kept verbatim
    bool operator==(const OcrResult&) const = default;
};
QString ocrKindToString(OcrResult::Kind k);

// Margins in pt. Global margins carry page="*" (or a range); per-page margins
// have no page attribute. origin is "auto" or "manual".
struct Margins {
    QString page;  // only for global margins
    double top = 0, bottom = 0, left = 0, right = 0;
    ZoneSource origin = ZoneSource::Auto;
    std::optional<double> confidence;
    bool operator==(const Margins&) const = default;
};

struct Zone {
    QString id;  // stable ID, e.g. p3-z1
    ZoneType type = ZoneType::Text;
    double x = 0, y = 0, w = 0, h = 0;  // pt, top-left origin of the unrotated page
    int order = 0;
    ZoneSource source = ZoneSource::Auto;
    bool locked = false;
    std::optional<double> confidence;
    QString lang, chapter, image;  // optional (empty = absent)
    QList<OcrResult> results;
    QList<OcrResult> resultsOfKind(OcrResult::Kind k) const;
    bool operator==(const Zone&) const = default;
};

struct ZonePage {
    int number = 1;  // 1-based
    double width = 0, height = 0;
    int rotation = 0;  // 0, 90, 180, 270
    std::optional<Margins> margins;
    QList<Zone> zones;
    bool operator==(const ZonePage&) const = default;
};

struct ZoneDocument {
    QString version = kZonesVersion;
    QString source, sourceSha256, generator, created;  // optional metadata
    QString unit = "pt";
    std::optional<Margins> margins;  // global margins
    QList<ZonePage> pages;

    const ZonePage* page(int number) const;
    const Zone* zone(const QString& id) const;
    bool operator==(const ZoneDocument&) const = default;
};

// Semantic checks beyond the XML structure: unique zone IDs, unique page
// numbers, non-negative sizes, confidences within [0,1], valid rotation,
// non-empty engine names. Returns one English message per problem.
QStringList validateZones(const ZoneDocument& doc);

}  // namespace pdfcore
