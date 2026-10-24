// Generated test corpus. Every file is synthesised here from code (text drawn
// with the standard Helvetica font, procedurally generated images, CJK text
// through the predefined Adobe CMaps without embedded fonts), so the corpus is
// covered by the project's own licence. See tests/corpus/README.md.
#include "Corpus.h"
#include "Fixtures.h"
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QStringList>
#include <memory>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <functional>
#include <QHash>

namespace corpus {
namespace {
using B = QByteArray;
B num(qint64 v) { return B::number(v); }
B ref(int id) { return num(id) + " 0 R"; }
B streamObj(const B& data, const B& extraDict = {}) {
    return "<< /Length " + num(data.size()) + (extraDict.isEmpty() ? B() : " " + extraDict) + " >>\nstream\n" + data + "\nendstream";
}
B pdfStr(const B& s) { B o = s; o.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)"); return "(" + o + ")"; }
B textOps(const B& text, double x = 72, double y = 700, int size = 18) {
    return "BT /F1 " + num(size) + " Tf " + B::number(x) + " " + B::number(y) + " Td " + pdfStr(text) + " Tj ET\n";
}

// Minimal PDF writer: object i is objs[i-1].
struct Pdf {
    QList<B> objs;
    int add(const B& body) { objs << body; return int(objs.size()); }
    int reserve() { objs << B(); return int(objs.size()); }
    void set(int id, const B& body) { objs[id - 1] = body; }
    B build(int root, int xrefShift = 0, bool badStartxref = false) const {
        B pdf = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
        std::vector<qsizetype> off;
        for (int i = 0; i < objs.size(); ++i) {
            off.push_back(pdf.size());
            pdf += num(i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n";
        }
        qsizetype xref = pdf.size();
        pdf += "xref\n0 " + num(objs.size() + 1) + "\n0000000000 65535 f \n";
        for (auto o : off) pdf += num(o + xrefShift).rightJustified(10, '0') + " 00000 n \n";
        pdf += "trailer\n<< /Size " + num(objs.size() + 1) + " /Root " + ref(root) + " >>\nstartxref\n"
             + num(badStartxref ? xref + 1234 : xref) + "\n%%EOF\n";
        return pdf;
    }
};

// Document skeleton: catalog + pages tree + Helvetica; addPage() appends pages.
struct Doc {
    Pdf p;
    int catalog, pages, font;
    QList<int> kids;
    B catalogExtra, pagesExtra;
    Doc() { catalog = p.reserve(); pages = p.reserve(); font = p.add("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"); }
    int addPage(const B& content, double w = 595, double h = 842, const B& extra = {}, const B& xobjects = {},
                const B& fonts = {}) {
        int c = p.add(streamObj(content));
        B res = "<< /Font << /F1 " + ref(font) + " " + fonts + " >>" + (xobjects.isEmpty() ? B() : " /XObject << " + xobjects + " >>") + " >>";
        int pg = p.add("<< /Type /Page /Parent " + ref(pages) + " /MediaBox [0 0 " + B::number(w) + " " + B::number(h)
                       + "] /Resources " + res + " /Contents " + ref(c) + (extra.isEmpty() ? B() : " " + extra) + " >>");
        kids << pg;
        return pg;
    }
    B build(int xrefShift = 0, bool badStartxref = false) {
        B k; for (int id : kids) k += ref(id) + " ";
        p.set(pages, "<< /Type /Pages /Count " + num(kids.size()) + " /Kids [" + k + "] " + pagesExtra + " >>");
        p.set(catalog, "<< /Type /Catalog /Pages " + ref(pages) + " " + catalogExtra + " >>");
        return p.build(catalog, xrefShift, badStartxref);
    }
};

struct WriteOpts {
    bool objStreams = false, linearize = false;
    enum Enc { None, AES256, AES128, RC4_128, RC4_40 } enc = None;
    const char* user = ""; const char* owner = "owner-pw";
};
// Normalises through QPDF (compresses streams, valid xref) and writes `path`.
void writeVia(const B& raw, const QString& path, const WriteOpts& o = {}) {
    QPDF q;
    q.processMemoryFile("corpus", raw.constData(), size_t(raw.size()));
    QPDFWriter w(q);
    w.setOutputMemory();
    w.setStaticID(true);
    if (o.objStreams) w.setObjectStreamMode(qpdf_o_generate);
    if (o.linearize) w.setLinearization(true);
    switch (o.enc) {
    case WriteOpts::None: break;
    case WriteOpts::AES256: w.setR6EncryptionParameters(o.user, o.owner, true, true, true, true, true, true, qpdf_r3p_full, true); break;
    case WriteOpts::AES128: w.setR4EncryptionParametersInsecure(o.user, o.owner, true, true, true, true, true, true, qpdf_r3p_full, true, true); break;
    case WriteOpts::RC4_128: w.setR3EncryptionParametersInsecure(o.user, o.owner, true, true, true, true, true, true, qpdf_r3p_full); break;
    case WriteOpts::RC4_40: w.setR2EncryptionParametersInsecure(o.user, o.owner, true, true, true, true); break;
    }
    w.write();
    auto buf = w.getBufferSharedPointer();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) throw std::runtime_error("cannot write corpus file");
    f.write(reinterpret_cast<const char*>(buf->getBuffer()), qint64(buf->getSize()));
}
void writeRaw(const B& raw, const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) throw std::runtime_error("cannot write corpus file");
    f.write(raw);
}

B textDoc(int n, const B& word, double w = 595, double h = 842) {
    Doc d;
    for (int i = 0; i < n; ++i) d.addPage(textOps("Page " + num(i + 1) + " " + word, 72, h - 100), w, h);
    return d.build();
}

// Procedural "scan": light background, dark text-like bars, some noise.
B scanPixels(int w, int h, int comps, int bpc, int seed) {
    B out;
    if (bpc == 1) {
        const int rowBytes = (w + 7) / 8;
        out.resize(qsizetype(rowBytes) * h);
        for (int y = 0; y < h; ++y) for (int xb = 0; xb < rowBytes; ++xb) {
            unsigned char v = 0xff;
            int line = (y / 24) % 3;
            if (line == 1 && ((xb + seed) % 11) < 8) v = 0x00;          // "text" rows
            out[qsizetype(y) * rowBytes + xb] = char(v);
        }
        return out;
    }
    out.resize(qsizetype(w) * h * comps);
    quint32 s = quint32(seed) * 2654435761u + 1;
    char* p = out.data();
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        s = s * 1664525u + 1013904223u;
        int base = 235 + int((s >> 24) & 7);                                // paper noise
        bool ink = ((y / 20) % 3 == 1) && (((x / 14) + seed) % 9 < 7);       // text-like bars
        int v = ink ? 30 + int((s >> 16) & 15) : base;
        for (int c = 0; c < comps; ++c) *p++ = char(std::clamp(v + c * 5 - 5, 0, 255));
    }
    return out;
}
// Adds an image XObject (raw, compressed later by QPDF); returns its id.
int addImage(Pdf& p, int w, int h, int comps, int bpc, int seed) {
    B cs = comps == 3 ? "/DeviceRGB" : "/DeviceGray";
    return p.add(streamObj(scanPixels(w, h, comps, bpc, seed),
        "/Type /XObject /Subtype /Image /Width " + num(w) + " /Height " + num(h) + " /ColorSpace " + cs
        + " /BitsPerComponent " + num(bpc)));
}
// Image-only page: image of `wIn` x `hIn` inches at `dpi`, filling the page.
void addScanPage(Doc& d, double wIn, double hIn, int dpi, int comps, int bpc, int seed) {
    int img = addImage(d.p, int(wIn * dpi), int(hIn * dpi), comps, bpc, seed);
    double W = wIn * 72, H = hIn * 72;
    d.addPage("q " + B::number(W) + " 0 0 " + B::number(H) + " 0 0 cm /Im0 Do Q\n", W, H, {}, "/Im0 " + ref(img));
}

B formDoc(int variant) {
    Doc d;
    QList<int> fields;
    auto widget = [&](int page, const B& dict) { int id = d.p.add("<< /Type /Annot /Subtype /Widget /P " + ref(page) + " /F 4 " + dict + " >>"); return id; };
    auto pageWithAnnots = [&](const B& text, int nAnnotsHint) { Q_UNUSED(nAnnotsHint); return d.addPage(textOps(text)); };
    QList<QPair<int, QList<int>>> pageAnnots;
    const int pagesN = variant == 1 ? 3 : variant == 4 ? 4 : 2;
    QList<int> pg;
    for (int i = 0; i < pagesN; ++i) pg << pageWithAnnots("Form " + num(variant) + " page " + num(i + 1), 0);
    QHash<int, QList<int>> annots;
    auto text = [&](int page, const B& name, const B& value, double y) {
        int f = widget(pg[page], "/FT /Tx /T " + pdfStr(name) + " /V " + pdfStr(value) + " /Rect [72 " + B::number(y) + " 300 " + B::number(y + 24) + "] /DA (/Helv 12 Tf 0 g)");
        fields << f; annots[pg[page]] << f;
    };
    switch (variant) {
    case 1: for (int i = 0; i < 3; ++i) for (int k = 0; k < 4; ++k) text(i, "p" + num(i + 1) + "_field" + num(k), "value " + num(k), 600 - 40 * k); break;
    case 2: {
        text(0, "name", "Ada", 600);
        int cb = widget(pg[0], "/FT /Btn /T (agree) /V /Off /AS /Off /Rect [72 540 90 558]");
        fields << cb; annots[pg[0]] << cb;
        int radio = d.p.reserve();
        int r1 = widget(pg[1], "/Parent " + ref(radio) + " /AS /Off /Rect [72 600 90 618]");
        int r2 = widget(pg[1], "/Parent " + ref(radio) + " /AS /Off /Rect [72 570 90 588]");
        d.p.set(radio, "<< /FT /Btn /Ff 49152 /T (choice) /V /Off /Kids [" + ref(r1) + " " + ref(r2) + "] >>");
        fields << radio; annots[pg[1]] << r1 << r2;
        break; }
    case 3: {
        int combo = widget(pg[0], "/FT /Ch /Ff 131072 /T (country) /Opt [(France) (Japan) (India)] /V (Japan) /Rect [72 600 300 624] /DA (/Helv 12 Tf 0 g)");
        int list = widget(pg[1], "/FT /Ch /T (colours) /Opt [(Red) (Green) (Blue)] /V (Green) /Rect [72 500 300 600] /DA (/Helv 12 Tf 0 g)");
        fields << combo << list; annots[pg[0]] << combo; annots[pg[1]] << list;
        break; }
    case 4: {
        int parent = d.p.reserve();
        QList<int> kids;
        for (int i = 0; i < 4; ++i) {
            int k = widget(pg[i], "/FT /Tx /Parent " + ref(parent) + " /T (line" + num(i) + ") /V (Street " + num(i) + ") /Rect [72 600 300 624] /DA (/Helv 12 Tf 0 g)");
            kids << k; annots[pg[i]] << k;
        }
        B k; for (int id : kids) k += ref(id) + " ";
        d.p.set(parent, "<< /T (address) /Kids [" + k + "] >>");
        fields << parent;
        break; }
    case 5: {
        text(0, "invoice", "INV-001", 600);
        text(1, "total", "42.00", 600);
        // Minimal XFA packet next to the AcroForm fields (viewers fall back to AcroForm).
        int xfa = d.p.add(streamObj("<?xml version=\"1.0\"?><xdp:xdp xmlns:xdp=\"http://ns.adobe.com/xdp/\">"
            "<template xmlns=\"http://www.xfa.org/schema/xfa-template/3.3/\"><subform name=\"form1\">"
            "<field name=\"invoice\"/><field name=\"total\"/></subform></template></xdp:xdp>"));
        d.catalogExtra = "/NeedsRendering false";
        fields << -xfa; // marker, handled below
        break; }
    }
    // Patch /Annots into each page.
    for (int i = 0; i < pg.size(); ++i) {
        if (!annots.contains(pg[i])) continue;
        B a; for (int id : annots[pg[i]]) a += ref(id) + " ";
        B& body = d.p.objs[pg[i] - 1];
        body.chop(3); body += " /Annots [" + a + "] >>";
    }
    B f; int xfa = 0;
    for (int id : fields) { if (id < 0) xfa = -id; else f += ref(id) + " "; }
    int acro = d.p.add("<< /Fields [" + f + "] /NeedAppearances true /DA (/Helv 0 Tf 0 g) /DR << /Font << /Helv " + ref(d.font) + " >> >>"
                       + (xfa ? " /XFA " + ref(xfa) : B()) + " >>");
    d.catalogExtra += " /AcroForm " + ref(acro);
    return d.build();
}

// Text in a predefined CJK CMap (UCS-2 code units), no embedded font.
B cjkDoc(const QString& text, const B& baseFont, const B& ordering, const B& cmap) {
    Doc d;
    int desc = d.p.add("<< /Type /FontDescriptor /FontName /" + baseFont + " /Flags 4 /FontBBox [0 -200 1000 900] /ItalicAngle 0 /Ascent 880 /Descent -120 /CapHeight 700 /StemV 80 >>");
    int cid = d.p.add("<< /Type /Font /Subtype /CIDFontType0 /BaseFont /" + baseFont + " /CIDSystemInfo << /Registry (Adobe) /Ordering ("
                      + ordering + ") /Supplement 2 >> /FontDescriptor " + ref(desc) + " /DW 1000 >>");
    int f2 = d.p.add("<< /Type /Font /Subtype /Type0 /BaseFont /" + baseFont + " /Encoding /" + cmap + " /DescendantFonts [" + ref(cid) + "] >>");
    B hex;
    for (QChar c : text) hex += B::number(c.unicode(), 16).rightJustified(4, '0');
    d.addPage(textOps("CJK sample") + "BT /F2 24 Tf 72 600 Td <" + hex + "> Tj ET\n", 595, 842, {}, {}, "/F2 " + ref(f2));
    d.addPage(textOps("Page 2") + "BT /F2 18 Tf 72 600 Td <" + hex + hex + "> Tj ET\n", 595, 842, {}, {}, "/F2 " + ref(f2));
    return d.build();
}
// Complex scripts: Identity-H with a ToUnicode CMap (code = UTF-16 unit), no
// embedded font. Exercises text extraction/search of Arabic or Devanagari.
B identityDoc(const QString& text, const B& baseFont) {
    Doc d;
    B bf;
    for (QChar c : text) { B h = B::number(c.unicode(), 16).rightJustified(4, '0'); bf += "<" + h + "> <" + h + ">\n"; }
    B cmap = "/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def "
             "/CMapName /Adobe-Identity-UCS def /CMapType 2 def 1 begincodespacerange <0000> <FFFF> endcodespacerange\n"
             + num(text.size()) + " beginbfchar\n" + bf + "endbfchar endcmap CMapName currentdict /CMap defineresource pop end end";
    int tu = d.p.add(streamObj(cmap));
    int desc = d.p.add("<< /Type /FontDescriptor /FontName /" + baseFont + " /Flags 4 /FontBBox [0 -300 1000 1000] /ItalicAngle 0 /Ascent 900 /Descent -300 /CapHeight 700 /StemV 80 >>");
    int cid = d.p.add("<< /Type /Font /Subtype /CIDFontType2 /BaseFont /" + baseFont + " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor "
                      + ref(desc) + " /DW 600 /CIDToGIDMap /Identity >>");
    int f2 = d.p.add("<< /Type /Font /Subtype /Type0 /BaseFont /" + baseFont + " /Encoding /Identity-H /DescendantFonts [" + ref(cid) + "] /ToUnicode " + ref(tu) + " >>");
    B hex;
    for (QChar c : text) hex += B::number(c.unicode(), 16).rightJustified(4, '0');
    d.addPage(textOps("Script sample") + "BT /F2 24 Tf 72 600 Td <" + hex + "> Tj ET\n", 595, 842, {}, {}, "/F2 " + ref(f2));
    return d.build();
}

struct Gen { Entry e; std::function<void(const QString&)> write; };

QList<Gen> generators() {
    QList<Gen> g;
    auto add = [&](Entry e, std::function<void(const QString&)> w) { g.push_back({std::move(e), std::move(w)}); };
    // --- complex forms (5)
    const int formPages[] = {3, 2, 2, 4, 2};
    for (int v = 1; v <= 5; ++v)
        add({QString("form-%1%2.pdf").arg(v).arg(v == 5 ? "-xfa" : ""), "forms", formPages[v - 1]},
            [v](const QString& p) { writeVia(formDoc(v), p); });
    // --- scanned, image only (5)
    add({"scan-300dpi-gray.pdf", "scanned", 2}, [](const QString& p) { Doc d; addScanPage(d, 4, 3, 300, 1, 8, 1); addScanPage(d, 4, 3, 300, 1, 8, 2); writeVia(d.build(), p); });
    add({"scan-600dpi-gray.pdf", "scanned", 1}, [](const QString& p) { Doc d; addScanPage(d, 3, 2, 600, 1, 8, 3); writeVia(d.build(), p); });
    add({"scan-300dpi-rgb.pdf", "scanned", 1}, [](const QString& p) { Doc d; addScanPage(d, 3, 3, 300, 3, 8, 4); writeVia(d.build(), p); });
    add({"scan-600dpi-bilevel.pdf", "scanned", 1}, [](const QString& p) { Doc d; addScanPage(d, 4, 4, 600, 1, 1, 5); writeVia(d.build(), p); });
    add({"scan-mixed-dpi.pdf", "scanned", 3}, [](const QString& p) {
        Doc d; addScanPage(d, 3, 2, 150, 1, 8, 6); addScanPage(d, 3, 2, 300, 3, 8, 7); addScanPage(d, 3, 2, 600, 1, 1, 8); writeVia(d.build(), p); });
    // --- encrypted (5); user password "user-pw", owner "owner-pw"
    add({"enc-aes256-user-owner.pdf", "encrypted", 2, "user-pw"}, [](const QString& p) { WriteOpts o; o.enc = WriteOpts::AES256; o.user = "user-pw"; writeVia(textDoc(2, "Secret"), p, o); });
    add({"enc-aes256-owner-only.pdf", "encrypted", 2}, [](const QString& p) { WriteOpts o; o.enc = WriteOpts::AES256; writeVia(textDoc(2, "Secret"), p, o); });
    add({"enc-rc4-128-user-owner.pdf", "encrypted", 2, "user-pw"}, [](const QString& p) { WriteOpts o; o.enc = WriteOpts::RC4_128; o.user = "user-pw"; writeVia(textDoc(2, "Secret"), p, o); });
    add({"enc-rc4-40-owner-only.pdf", "encrypted", 2}, [](const QString& p) { WriteOpts o; o.enc = WriteOpts::RC4_40; writeVia(textDoc(2, "Secret"), p, o); });
    add({"enc-aes128-user.pdf", "encrypted", 2, "user-pw"}, [](const QString& p) { WriteOpts o; o.enc = WriteOpts::AES128; o.user = "user-pw"; o.owner = "user-pw"; writeVia(textDoc(2, "Secret"), p, o); });
    // --- large, >100 pages (5); generated at test time to keep the repo small
    add({"large-500p-text.pdf", "large", 500}, [](const QString& p) { writeVia(textDoc(500, "Lorem"), p); });
    add({"large-200p-images.pdf", "large", 200}, [](const QString& p) {
        Doc d; for (int i = 0; i < 200; ++i) addScanPage(d, 2, 2, 48, 1, 8, i); writeVia(d.build(), p); });
    add({"large-120p-objstm.pdf", "large", 120}, [](const QString& p) { WriteOpts o; o.objStreams = true; writeVia(textDoc(120, "Objstm"), p, o); });
    add({"large-300p-mixed-sizes.pdf", "large", 300}, [](const QString& p) {
        Doc d; for (int i = 0; i < 300; ++i) { bool l = i % 3 == 0; d.addPage(textOps("Page " + num(i + 1)), l ? 842 : 595, l ? 595 : 842); } writeVia(d.build(), p); });
    add({"large-150p-shared-image.pdf", "large", 150}, [](const QString& p) {
        Doc d; int img = addImage(d.p, 200, 200, 3, 8, 9);
        for (int i = 0; i < 150; ++i) d.addPage(textOps("Page " + num(i + 1)) + "q 200 0 0 200 72 300 cm /Im0 Do Q\n", 595, 842, {}, "/Im0 " + ref(img));
        writeVia(d.build(), p); });
    // --- CJK / Unicode (5)
    add({QString::fromUtf8("unicode-日本語.pdf"), "unicode", 2}, [](const QString& p) { writeVia(cjkDoc(QString::fromUtf8("日本語のテキスト"), "HeiseiMin-W3", "Japan1", "UniJIS-UCS2-H"), p); });
    add({"unicode-chinese.pdf", "unicode", 2}, [](const QString& p) { writeVia(cjkDoc(QString::fromUtf8("中文文本测试"), "STSong-Light", "GB1", "UniGB-UCS2-H"), p); });
    add({"unicode-korean.pdf", "unicode", 2}, [](const QString& p) { writeVia(cjkDoc(QString::fromUtf8("한국어 텍스트"), "HYSMyeongJo-Medium", "Korea1", "UniKS-UCS2-H"), p); });
    add({QString::fromUtf8("unicode-عربي.pdf"), "unicode", 1}, [](const QString& p) { writeVia(identityDoc(QString::fromUtf8("مرحبا بالعالم"), "ArabicSample"), p); });
    add({QString::fromUtf8("unicode-हिन्दी.pdf"), "unicode", 1}, [](const QString& p) { writeVia(identityDoc(QString::fromUtf8("नमस्ते दुनिया"), "DevanagariSample"), p); });
    // --- mixed page sizes (4)
    add({"sizes-a4-letter-a3-a5.pdf", "sizes", 4}, [](const QString& p) {
        Doc d; d.addPage(textOps("A4"), 595, 842); d.addPage(textOps("Letter"), 612, 792); d.addPage(textOps("A3"), 842, 1191); d.addPage(textOps("A5", 40, 500), 420, 595); writeVia(d.build(), p); });
    add({"sizes-tiny-1in.pdf", "sizes", 1}, [](const QString& p) { Doc d; d.addPage(textOps("x", 10, 30, 12), 72, 72); writeVia(d.build(), p); });
    add({"sizes-huge-200in.pdf", "sizes", 1}, [](const QString& p) { Doc d; d.addPage(textOps("Huge", 72, 14000, 400), 14400, 14400); writeVia(d.build(), p); });
    add({"sizes-offset-cropbox.pdf", "sizes", 2}, [](const QString& p) {
        Doc d;
        d.addPage(textOps("Offset", 172, 742), 595, 842, "/CropBox [100 100 500 800]");
        d.addPage(textOps("Origin", 150, 150), 595, 842);
        d.p.objs[d.kids.last() - 1].replace("/MediaBox [0 0 595 842]", "/MediaBox [100 100 695 942]");
        writeVia(d.build(), p); });
    // --- malformed but repairable (4); written raw, never through QPDF
    add({"malformed-bad-xref-offsets.pdf", "malformed", 3}, [](const QString& p) { writeRaw([] { Doc d; for (int i = 0; i < 3; ++i) d.addPage(textOps("Broken xref " + num(i + 1))); return d.build(7); }(), p); });
    add({"malformed-bad-startxref.pdf", "malformed", 2}, [](const QString& p) { writeRaw([] { Doc d; for (int i = 0; i < 2; ++i) d.addPage(textOps("Bad startxref " + num(i + 1))); return d.build(0, true); }(), p); });
    add({"malformed-missing-object.pdf", "malformed", 2}, [](const QString& p) {
        Doc d; d.addPage(textOps("Has a dangling reference")); d.addPage(textOps("Second"));
        // Page 1 points at a /Contents object and a font that do not exist.
        B& pg = d.p.objs[d.kids.first() - 1];
        pg.replace("/Contents " + ref(d.kids.first() - 1), "/Contents [" + ref(d.kids.first() - 1) + " 999 0 R]");
        pg.replace("/F1 " + ref(d.font), "/F1 " + ref(d.font) + " /F9 998 0 R");
        writeRaw(d.build(), p); });
    add({"malformed-bad-stream-length.pdf", "malformed", 2}, [](const QString& p) {
        Doc d; d.addPage(textOps("Wrong stream length")); d.addPage(textOps("Second page"));
        B& c = d.p.objs[d.kids.first() - 2]; // content stream of page 1
        c.replace(QByteArray("/Length "), QByteArray("/Length 9")); // e.g. 39 -> 939: far too long
        writeRaw(d.build(), p); });
    // --- rotation (4)
    add({"rotate-90.pdf", "rotation", 2, {}, {90, 90}}, [](const QString& p) { Doc d; d.addPage(textOps("R90 a"), 595, 842, "/Rotate 90"); d.addPage(textOps("R90 b"), 595, 842, "/Rotate 90"); writeVia(d.build(), p); });
    add({"rotate-180.pdf", "rotation", 1, {}, {180}}, [](const QString& p) { Doc d; d.addPage(textOps("R180"), 595, 842, "/Rotate 180"); writeVia(d.build(), p); });
    add({"rotate-270.pdf", "rotation", 1, {}, {270}}, [](const QString& p) { Doc d; d.addPage(textOps("R270"), 595, 842, "/Rotate 270"); writeVia(d.build(), p); });
    add({"rotate-inherited-mixed.pdf", "rotation", 3, {}, {90, 270, 0}}, [](const QString& p) {
        Doc d; d.pagesExtra = "/Rotate 90";                                // inherited by page 1
        d.addPage(textOps("inherited 90")); d.addPage(textOps("own -90"), 595, 842, "/Rotate -90"); d.addPage(textOps("own 0"), 595, 842, "/Rotate 0");
        writeRaw(d.build(), p); });
    // --- linearised (3)
    add({"linearized-text-10p.pdf", "linearized", 10}, [](const QString& p) { WriteOpts o; o.linearize = true; writeVia(textDoc(10, "Fast"), p, o); });
    add({"linearized-form.pdf", "linearized", 3}, [](const QString& p) { WriteOpts o; o.linearize = true; writeVia(formDoc(1), p, o); });
    add({"linearized-scan.pdf", "linearized", 1}, [](const QString& p) { WriteOpts o; o.linearize = true; Doc d; addScanPage(d, 3, 2, 300, 1, 8, 11); writeVia(d.build(), p, o); });
    // --- empty / 1-page (2)
    add({"minimal-empty-content.pdf", "minimal", 1}, [](const QString& p) { Doc d; d.addPage(""); writeVia(d.build(), p); });
    add({"minimal-no-contents.pdf", "minimal", 1}, [](const QString& p) {
        Doc d; d.addPage("");
        B& pg = d.p.objs[d.kids.first() - 1];
        pg = "<< /Type /Page /Parent " + ref(d.pages) + " /MediaBox [0 0 612 792] >>"; // no /Resources, no /Contents
        writeRaw(d.build(), p); });
    return g;
}

std::unique_ptr<QTemporaryDir> g_dir;
QList<Entry> g_entries;

void generate(const QString& outDir, QList<Entry>* list) {
    QDir d(outDir);
    // The 8 original fixtures (tests/Fixtures.cpp).
    const struct { const char* n; int pages; bool garbage; } base[] = {
        {"one.pdf", 1, false}, {"three.pdf", 3, false}, {"five.pdf", 5, false}, {"landscape.pdf", 2, false},
        {"unicode é.pdf", 2, false}, {"garbage.pdf", 0, true}, {"form.pdf", 1, false}, {"objstm.pdf", 5, false}};
    for (const auto& b : base) {
        const QString n = QString::fromUtf8(b.n);
        QFile::remove(d.filePath(n));
        QFile::copy(fixtures::path(n), d.filePath(n));
        Entry e; e.name = n; e.category = "base"; e.pages = b.pages; e.garbage = b.garbage;
        if (list) list->push_back(e);
    }
    for (auto& gen : generators()) {
        gen.write(d.filePath(gen.e.name));
        if (list) list->push_back(gen.e);
    }
}
}

QString dir() {
    if (!g_dir) {
        g_dir = std::make_unique<QTemporaryDir>();
        generate(g_dir->path(), &g_entries);
    }
    return g_dir->path();
}
QList<Entry> entries() { dir(); return g_entries; }
QString path(const QString& name) { return QDir(dir()).filePath(name); }
int writeTo(const QString& outDir) {
    QDir().mkpath(outDir);
    QList<Entry> l;
    generate(outDir, &l);
    return int(l.size());
}
}
