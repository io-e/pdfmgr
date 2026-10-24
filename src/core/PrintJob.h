#pragma once
#include <QString>
namespace pdfcore {
struct PrintJob {
    enum class Scaling { Fit, Shrink, NoScale };
    enum class Orientation { Auto, Portrait, Landscape };
    enum class Duplex { Default, Simplex, DuplexLong, DuplexShort };
    enum class Color { Default, Color, Monochrome };

    QString file;            // PDF to print (PrintService fills it with a temp copy)
    QString printer;         // empty = default printer
    QString pageRange;       // "1-3,5", "odd", "even" or empty = all
    Scaling scaling = Scaling::Shrink;
    Orientation orientation = Orientation::Auto;
    Duplex duplex = Duplex::Default;
    Color color = Color::Default;
    QString paper;           // e.g. "A4"
    int bin = -1;            // paper tray, -1 = default
    int copies = 1;
};
}
