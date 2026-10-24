#include "Version.h"
#include <mupdf/fitz/version.h>
#include <qpdf/QPDF.hh>

namespace pdfcore {
QString mupdfVersion() { return QStringLiteral(FZ_VERSION); }
QString qpdfVersion() { return QString::fromLatin1(QPDF::QPDFVersion()); }
}
