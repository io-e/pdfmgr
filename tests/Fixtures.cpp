#include "Fixtures.h"
#include <QTemporaryDir>
#include <QFile>
#include <QProcess>
#include <QDir>
#include <vector>
#include <memory>

namespace fixtures {
void writePdf(const QString& path, int n, const QString& word, QSizeF size) {
    QByteArray pdf = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<qsizetype> off;
    auto obj = [&](const QByteArray& body) { off.push_back(pdf.size());
        pdf += QByteArray::number(off.size()) + " 0 obj\n" + body + "\nendobj\n"; };
    // 1 catalog, 2 pages, 3 font, then (page, content) pairs
    QByteArray kids;
    for (int i = 0; i < n; ++i) kids += QByteArray::number(4 + 2 * i) + " 0 R ";
    obj("<< /Type /Catalog /Pages 2 0 R >>");
    obj("<< /Type /Pages /Count " + QByteArray::number(n) + " /Kids [" + kids + "] >>");
    obj("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    for (int i = 0; i < n; ++i) {
        QByteArray stream = "BT /F1 24 Tf 72 " + QByteArray::number(int(size.height()) - 100) + " Td (Page "
            + QByteArray::number(i + 1) + " " + word.toLatin1() + ") Tj ET";
        obj("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + QByteArray::number(int(size.width())) + " "
            + QByteArray::number(int(size.height())) + "] /Resources << /Font << /F1 3 0 R >> >> /Contents "
            + QByteArray::number(5 + 2 * i) + " 0 R >>");
        obj("<< /Length " + QByteArray::number(stream.size()) + " >>\nstream\n" + stream + "\nendstream");
    }
    qsizetype xref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(off.size() + 1) + "\n0000000000 65535 f \n";
    for (auto o : off) pdf += QByteArray::number(o).rightJustified(10, '0') + " 00000 n \n";
    pdf += "trailer\n<< /Size " + QByteArray::number(off.size() + 1) + " /Root 1 0 R >>\nstartxref\n"
         + QByteArray::number(xref) + "\n%%EOF\n";
    QFile f(path); f.open(QIODevice::WriteOnly); f.write(pdf);
}

static std::unique_ptr<QTemporaryDir> g_dir;
QString dir() {
    if (!g_dir) {
        g_dir = std::make_unique<QTemporaryDir>();
        QDir d(g_dir->path());
        writePdf(d.filePath("one.pdf"), 1, "Alpha");
        writePdf(d.filePath("three.pdf"), 3, "Bravo");
        writePdf(d.filePath("five.pdf"), 5, "Charlie");
        writePdf(d.filePath("landscape.pdf"), 2, "Delta", {842, 595});
        writePdf(d.filePath("unicode é.pdf"), 2, "Echo");
        QFile g(d.filePath("garbage.pdf")); g.open(QIODevice::WriteOnly); g.write("this is not a pdf");
        // qpdf-generated variant with object streams (exercise a different file structure)
        QProcess::execute(QPDF_EXE, {"--object-streams=generate", d.filePath("five.pdf"), d.filePath("objstm.pdf")});
    }
    return g_dir->path();
}
QString path(const QString& name) { return QDir(dir()).filePath(name); }
QString tmpFile(const QString& name) { return QDir(dir()).filePath("out-" + name); }
int qpdfCheck(const QString& file, QString* out) {
    QProcess p; p.start(QPDF_EXE, {"--check", file}); p.waitForFinished(30000);
    if (out) *out = QString::fromLocal8Bit(p.readAllStandardOutput() + p.readAllStandardError());
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}
}
