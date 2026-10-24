#include "SumatraBackend.h"
#include <QCoreApplication>
#include <QDir>
namespace pdfcore {
QString SumatraBackend::defaultExePath() {
    QString dir = QCoreApplication::instance() ? QCoreApplication::applicationDirPath() : QDir::currentPath();
    return QDir(dir).filePath(QStringLiteral("SumatraPDF.exe"));
}
QString SumatraBackend::settingsString(const PrintJob& j) {
    QStringList s;
    QString r = j.pageRange; r.remove(' ');
    if (!r.isEmpty()) s << r;
    switch (j.scaling) {
    case PrintJob::Scaling::Fit: s << "fit"; break;
    case PrintJob::Scaling::Shrink: s << "shrink"; break;
    case PrintJob::Scaling::NoScale: s << "noscale"; break;
    }
    if (j.orientation == PrintJob::Orientation::Portrait) s << "portrait";
    else if (j.orientation == PrintJob::Orientation::Landscape) s << "landscape";
    switch (j.duplex) {
    case PrintJob::Duplex::Simplex: s << "simplex"; break;
    case PrintJob::Duplex::DuplexLong: s << "duplexlong"; break;
    case PrintJob::Duplex::DuplexShort: s << "duplexshort"; break;
    default: break;
    }
    if (j.color == PrintJob::Color::Color) s << "color";
    else if (j.color == PrintJob::Color::Monochrome) s << "monochrome";
    if (!j.paper.isEmpty()) s << "paper=" + j.paper;
    if (j.bin >= 0) s << "bin=" + QString::number(j.bin);
    if (j.copies > 1) s << QString::number(j.copies) + "x";
    return s.join(',');
}
QStringList SumatraBackend::arguments(const PrintJob& j) const {
    QStringList a;
    if (j.printer.isEmpty()) a << "-print-to-default";
    else a << "-print-to" << j.printer;
    a << "-print-settings" << settingsString(j) << "-silent" << QDir::toNativeSeparators(j.file);
    return a;
}
}
