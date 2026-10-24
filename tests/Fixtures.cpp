#include "Fixtures.h"
#include <QTemporaryDir>
#include <QFile>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QPDFJob.hh>
#include <qpdf/QPDFLogger.hh>
#include <sstream>
#include <stdexcept>
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
        writeFormPdf(d.filePath("form.pdf"), "name", "Foxtrot");
        // qpdf-generated variant with object streams (exercise a different file structure)
        QPDF q; q.processFile(d.filePath("five.pdf").toUtf8().constData());
        QPDFWriter w(q, d.filePath("objstm.pdf").toUtf8().constData());
        w.setObjectStreamMode(qpdf_o_generate);
        w.write();
        // Encrypted files (fixture passwords are test data, not secrets).
        writeEncrypted(d.filePath("three.pdf"), d.filePath("enc-aes256-user.pdf"), "user-pw", "owner-pw", Crypt::AES256);
        writeEncrypted(d.filePath("three.pdf"), d.filePath("enc-aes256-owner-only.pdf"), "", "owner-pw", Crypt::AES256);
        writeEncrypted(d.filePath("three.pdf"), d.filePath("enc-rc4-user.pdf"), "user-pw", "owner-pw", Crypt::RC4_128);
        writeEncrypted(d.filePath("three.pdf"), d.filePath("enc-aes128-user.pdf"), "user-pw", "owner-pw", Crypt::AES128);
    }
    return g_dir->path();
}
void writeEncrypted(const QString& in, const QString& out, const char* user, const char* owner, Crypt c) {
    QPDF q; q.processFile(in.toUtf8().constData());
    QPDFWriter w(q, out.toUtf8().constData());
    w.setStaticID(true); // reproducible output
    switch (c) {
    case Crypt::AES256: w.setR6EncryptionParameters(user, owner, true, true, true, true, true, true, qpdf_r3p_full, true); break;
    case Crypt::AES128: w.setR4EncryptionParametersInsecure(user, owner, true, true, true, true, true, true, qpdf_r3p_full, true, true); break;
    case Crypt::RC4_128: w.setR3EncryptionParametersInsecure(user, owner, true, true, true, true, true, true, qpdf_r3p_full); break;
    }
    w.write();
}
bool isEncrypted(const QString& file, const char* password) {
    QPDF q; q.processFile(file.toUtf8().constData(), password);
    return q.isEncrypted();
}
QString path(const QString& name) { return QDir(dir()).filePath(name); }
QString tmpFile(const QString& name) { return QDir(dir()).filePath("out-" + name); }
// Equivalent of `qpdf --check file`, through the library (no qpdf executable).
// Returns 0 when clean, like the CLI.
int qpdfCheck(const QString& file, QString* out) {
    std::ostringstream log;
    try {
        QPDFJob j;
        auto logger = QPDFLogger::create();
        logger->setOutputStreams(&log, &log);
        j.setLogger(logger);
        QByteArray f = file.toUtf8(); // QPDFJob paths are UTF-8 (wide on Windows internally)
        j.config()->inputFile(f.constData())->check()->checkConfiguration();
        j.run();
        if (out) *out = QString::fromStdString(log.str());
        return j.hasWarnings() ? 3 : j.getExitCode();
    } catch (std::exception& e) {
        if (out) *out = QString::fromStdString(log.str()) + e.what();
        return 2;
    }
}

// One page with one AcroForm text field `field` (value `value`) and a bookmark
// "Section" pointing to the page.
void writeFormPdf(const QString& path, const QString& field, const QString& value) {
    QByteArray f = field.toLatin1(), v = value.toLatin1();
    QByteArray stream = "BT /F1 18 Tf 72 700 Td (Form " + v + ") Tj ET";
    QList<QByteArray> objs = {
        "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [5 0 R] /NeedAppearances true /DA (/Helv 0 Tf 0 g) "
        "/DR << /Font << /Helv 3 0 R >> >> >> /Outlines 7 0 R >>",
        "<< /Type /Pages /Count 1 /Kids [4 0 R] >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> "
        "/Contents 6 0 R /Annots [5 0 R] >>",
        "<< /Type /Annot /Subtype /Widget /FT /Tx /T (" + f + ") /V (" + v + ") /Rect [72 600 300 630] /P 4 0 R "
        "/DA (/Helv 12 Tf 0 g) /F 4 >>",
        "<< /Length " + QByteArray::number(stream.size()) + " >>\nstream\n" + stream + "\nendstream",
        "<< /Type /Outlines /First 8 0 R /Last 8 0 R /Count 1 >>",
        "<< /Title (Section) /Parent 7 0 R /Dest [4 0 R /Fit] >>",
    };
    QByteArray pdf = "%PDF-1.7\n";
    std::vector<qsizetype> off;
    for (int i = 0; i < objs.size(); ++i) {
        off.push_back(pdf.size());
        pdf += QByteArray::number(i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n";
    }
    qsizetype xref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(objs.size() + 1) + "\n0000000000 65535 f \n";
    for (auto o : off) pdf += QByteArray::number(o).rightJustified(10, '0') + " 00000 n \n";
    pdf += "trailer\n<< /Size " + QByteArray::number(objs.size() + 1) + " /Root 1 0 R >>\nstartxref\n"
         + QByteArray::number(xref) + "\n%%EOF\n";
    QFile out(path); out.open(QIODevice::WriteOnly); out.write(pdf);
}
}
