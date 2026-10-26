#pragma once
// Read/write the operational zone XML (spec A1, section 3.1) with
// QXmlStreamReader/Writer. Qt has no XSD validator, so reading performs the
// structural checks that schemas/pdfmgr-zones.xsd expresses, then
// validateZones() for the semantic ones.
#include "ZoneModel.h"
#include <QByteArray>
#include <optional>

namespace pdfcore {

struct ZoneXmlError {
    QString message;  // English
    qint64 line = 0, column = 0;
    QString toString() const;
};

// Parses a zone XML document. On failure returns nullopt and fills `error`.
std::optional<ZoneDocument> readZonesXml(const QByteArray& xml, ZoneXmlError* error = nullptr);
std::optional<ZoneDocument> readZonesFile(const QString& path, ZoneXmlError* error = nullptr);

// Serializes to indented UTF-8 XML. OCR result text is written verbatim.
QByteArray writeZonesXml(const ZoneDocument& doc);
// Atomic write (AtomicWrite). Refuses documents that fail validateZones().
bool writeZonesFile(const QString& path, const ZoneDocument& doc, QString* error = nullptr);

}  // namespace pdfcore
