# Zone XML format (Phase A1)

Operational zone XML for the zone-based OCR extension (spec `SPEC_pdfmgr_Zones_OCR_CLAUDE_V1`, section 3.1).
XML is the primary format; JSON, PAGE-XML, ALTO and YAML margins are export formats (Phase A2, `pdfmgr-cli zones export`).

- Namespace: `urn:io-e:pdfmgr:zones:1`, version `1.0`, unit `pt` (top-left origin of the unrotated page).
- Schema: [`schemas/pdfmgr-zones.xsd`](../schemas/pdfmgr-zones.xsd) (use it with the VS Code Red Hat XML extension or `xmllint --schema`).
- Example: [`tests/zones/sample.zones.xml`](../tests/zones/sample.zones.xml).
- Code: `pdfcore::ZoneDocument` (`src/core/ZoneModel.h`), `readZonesXml/readZonesFile/writeZonesXml/writeZonesFile` (`src/core/ZoneXmlIO.h`).

Structure: `<pdfZones>` → optional global `<margins page="*">` → `<page number width height rotation>` → optional page `<margins>` → `<zone>` elements.
A zone has `id type x y w h order source` (required) and `locked confidence lang chapter image` (optional).
Its children `<text>`, `<latex>` and `<alt>` each carry `engine` (required) and `confidence` (optional).
A zone may hold any number of them, so the candidates from every OCR engine are kept until a final version is chosen.

Qt has no XSD validator, so the reader enforces the same structure (namespace, required and unknown attributes, enums, numbers)
plus semantic checks (`validateZones`: unique zone IDs and page numbers, non-negative sizes, confidences in [0,1], rotation).
Errors are reported in English with line and column. When `xmllint` is installed, ctest also validates the sample and a written file against the XSD (Ubuntu CI).
