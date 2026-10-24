#include "CupsBackend.h"
namespace pdfcore {
QStringList CupsBackend::arguments(const PrintJob& j) const {
    QStringList a;
    if (!j.printer.isEmpty()) a << "-d" << j.printer;
    if (j.copies > 1) a << "-n" << QString::number(j.copies);
    const QString r = j.pageRange.trimmed().toLower();
    if (r == "odd" || r == "even") a << "-o" << ("page-set=" + r);
    else if (!r.isEmpty()) a << "-o" << ("page-ranges=" + r);
    switch (j.scaling) {
    case PrintJob::Scaling::Fit: a << "-o" << "fit-to-page"; break;
    case PrintJob::Scaling::Shrink: break; // CUPS default shrinks oversize pages
    case PrintJob::Scaling::NoScale: a << "-o" << "print-scaling=none"; break;
    }
    if (j.orientation == PrintJob::Orientation::Landscape) a << "-o" << "landscape";
    else if (j.orientation == PrintJob::Orientation::Portrait) a << "-o" << "portrait";
    switch (j.duplex) {
    case PrintJob::Duplex::Simplex: a << "-o" << "sides=one-sided"; break;
    case PrintJob::Duplex::DuplexLong: a << "-o" << "sides=two-sided-long-edge"; break;
    case PrintJob::Duplex::DuplexShort: a << "-o" << "sides=two-sided-short-edge"; break;
    default: break;
    }
    if (j.color == PrintJob::Color::Monochrome) a << "-o" << "print-color-mode=monochrome";
    else if (j.color == PrintJob::Color::Color) a << "-o" << "print-color-mode=color";
    if (!j.paper.isEmpty()) a << "-o" << ("media=" + j.paper);
    if (j.bin >= 0) a << "-o" << ("InputSlot=" + QString::number(j.bin));
    a << "--" << j.file;
    return a;
}
}
