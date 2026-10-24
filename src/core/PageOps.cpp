#include "PageOps.h"
#include "AtomicWrite.h"
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFOutlineDocumentHelper.hh>
#include <qpdf/QPDFOutlineObjectHelper.hh>
#include <qpdf/FileInputSource.hh>
#include <qpdf/Buffer.hh>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

namespace pdfcore::PageOps {
namespace {
// Opens with a wide-character path on Windows (any Unicode file name), and the
// native 8-bit encoding elsewhere. Never goes through the ANSI code page.
FILE* openFile(const QString& path, bool write) {
#ifdef Q_OS_WIN
    return _wfopen(reinterpret_cast<const wchar_t*>(path.utf16()), write ? L"wb" : L"rb");
#else
    return std::fopen(QFile::encodeName(path).constData(), write ? "wb" : "rb");
#endif
}
std::shared_ptr<QPDF> load(const QString& path) {
    FILE* f = openFile(path, false);
    if (!f) throw std::runtime_error("cannot open " + path.toStdString());
    auto src = std::make_shared<FileInputSource>(path.toUtf8().constData(), f, true); // owns f
    auto q = std::make_shared<QPDF>();
    q->processInputSource(src);
    return q;
}
QByteArray writeToMemory(QPDF& q) {
    QPDFWriter w(q);
    w.setOutputMemory();
    w.setObjectStreamMode(qpdf_o_preserve);
    w.write();
    auto buf = w.getBufferSharedPointer();
    return QByteArray(reinterpret_cast<const char*>(buf->getBuffer()), qsizetype(buf->getSize()));
}
// Output goes through QSaveFile (temp file + atomic rename), which already uses
// wide-character Win32 APIs, so Unicode output paths are safe on Windows too.
void save(QPDF& q, const QString& out, OpResult& r) {
    QString err;
    if (!atomicWrite(out, writeToMemory(q), &err)) throw std::runtime_error(err.toStdString());
    r.outputs << out;
}

struct OutlineNode { std::string title; QPDFObjectHandle page; std::vector<OutlineNode> kids; };

std::vector<OutlineNode> collectOutlines(std::vector<QPDFOutlineObjectHelper> items,
                                         const std::map<QPDFObjGen, QPDFObjectHandle>& pageMap, int depth = 0) {
    std::vector<OutlineNode> out;
    if (depth > 32) return out; // guard against malicious cycles
    for (auto& o : items) {
        OutlineNode n;
        n.title = o.getTitle();
        auto dp = o.getDestPage();
        if (dp.isDictionary()) {
            auto it = pageMap.find(dp.getObjGen());
            if (it != pageMap.end()) n.page = it->second;
        }
        n.kids = collectOutlines(o.getKids(), pageMap, depth + 1);
        if (n.page.isInitialized() || !n.kids.empty()) out.push_back(std::move(n));
    }
    return out;
}

// Writes `nodes` as children of `parent`; returns the number of visible descendants.
int writeOutlines(QPDF& dst, QPDFObjectHandle parent, const std::vector<OutlineNode>& nodes) {
    std::vector<QPDFObjectHandle> objs;
    int count = 0;
    for (auto& n : nodes) {
        auto d = dst.makeIndirectObject(QPDFObjectHandle::newDictionary());
        d.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(n.title));
        d.replaceKey("/Parent", parent);
        if (n.page.isInitialized())
            d.replaceKey("/Dest", QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{n.page, QPDFObjectHandle::newName("/Fit")}));
        if (!n.kids.empty()) count += writeOutlines(dst, d, n.kids);
        objs.push_back(d);
        ++count;
    }
    for (size_t i = 0; i < objs.size(); ++i) {
        if (i > 0) objs[i].replaceKey("/Prev", objs[i - 1]);
        if (i + 1 < objs.size()) objs[i].replaceKey("/Next", objs[i + 1]);
    }
    if (!objs.empty()) {
        parent.replaceKey("/First", objs.front());
        parent.replaceKey("/Last", objs.back());
        parent.replaceKey("/Count", QPDFObjectHandle::newInteger(count));
    }
    return count;
}
// Build a new document containing `pages` of src, in order.
void buildSubset(QPDF& src, const QList<int>& pages, const QString& out, OpResult& r) {
    QPDF dst; dst.emptyPDF();
    auto all = QPDFPageDocumentHelper(src).getAllPages();
    QPDFPageDocumentHelper dh(dst);
    for (int p : pages) {
        if (p < 0 || p >= int(all.size())) throw std::runtime_error("page index out of range");
        dh.addPage(all[size_t(p)], false);
    }
    save(dst, out, r);
}
template <class F> OpResult guarded(F f) {
    OpResult r;
    try { f(r); r.ok = true; }
    catch (std::exception& e) { r.ok = false; r.error = QString::fromUtf8(e.what()); }
    return r;
}
}

int pageCount(const QString& in, QString* error) {
    try { auto q = load(in); return int(QPDFPageDocumentHelper(*q).getAllPages().size()); }
    catch (std::exception& e) { if (error) *error = e.what(); return -1; }
}

OpResult merge(const QStringList& inputs, const QString& out) {
    return guarded([&](OpResult& r) {
        if (inputs.isEmpty()) throw std::runtime_error("no inputs");
        std::vector<std::shared_ptr<QPDF>> srcs; // must outlive the write
        QPDF dst; dst.emptyPDF();
        QPDFPageDocumentHelper dh(dst);
        QPDFAcroFormDocumentHelper dstForms(dst);
        std::vector<OutlineNode> outline;
        bool needAppearances = false;
        for (const auto& in : inputs) {
            srcs.push_back(load(in));
            QPDF& src = *srcs.back();
            QPDFAcroFormDocumentHelper srcForms(src);
            const bool hasForm = srcForms.hasAcroForm();
            std::map<QPDFObjGen, QPDFObjectHandle> pageMap; // source page -> copied page
            QPDFObjectHandle first;
            for (auto& page : QPDFPageDocumentHelper(src).getAllPages()) {
                dh.addPage(page, false);
                QPDFObjectHandle copied = dst.getAllPages().back();
                // Copies widget annotations' fields into the destination /AcroForm,
                // renaming conflicting field names.
                if (hasForm) dstForms.fixCopiedAnnotations(copied, page.getObjectHandle(), srcForms);
                pageMap[page.getObjectHandle().getObjGen()] = copied;
                if (!first.isInitialized()) first = copied;
            }
            if (hasForm) needAppearances |= srcForms.getNeedAppearances();
            if (!first.isInitialized()) continue;
            // One bookmark per input file, containing that file's own outline.
            OutlineNode fileNode;
            fileNode.title = QFileInfo(in).completeBaseName().toStdString();
            fileNode.page = first;
            QPDFOutlineDocumentHelper odh(src);
            if (odh.hasOutlines()) fileNode.kids = collectOutlines(odh.getTopLevelOutlines(), pageMap);
            outline.push_back(std::move(fileNode));
        }
        if (dstForms.hasAcroForm() && needAppearances) dstForms.setNeedAppearances(true);
        if (inputs.size() > 1 || !outline.empty()) {
            bool anyKids = std::any_of(outline.begin(), outline.end(), [](const OutlineNode& n) { return !n.kids.empty(); });
            if (inputs.size() > 1 || anyKids) {
                auto root = dst.makeIndirectObject(QPDFObjectHandle::parse("<< /Type /Outlines >>"));
                writeOutlines(dst, root, inputs.size() > 1 ? outline : outline.front().kids);
                dst.getRoot().replaceKey("/Outlines", root);
            }
        }
        save(dst, out, r);
    });
}

OpResult split(const QString& in, const QString& outPrefix, int per) {
    return guarded([&](OpResult& r) {
        if (per < 1) throw std::runtime_error("pagesPerFile must be >= 1");
        auto src = load(in);
        int n = int(QPDFPageDocumentHelper(*src).getAllPages().size());
        for (int start = 0, idx = 1; start < n; start += per, ++idx) {
            QList<int> pages;
            for (int p = start; p < std::min(n, start + per); ++p) pages << p;
            buildSubset(*src, pages, QStringLiteral("%1-%2.pdf").arg(outPrefix).arg(idx), r);
        }
    });
}

OpResult extract(const QString& in, const QList<int>& pages, const QString& out) {
    return guarded([&](OpResult& r) {
        if (pages.isEmpty()) throw std::runtime_error("no pages selected");
        auto src = load(in);
        buildSubset(*src, pages, out, r);
    });
}

OpResult reorder(const QString& in, const QList<int>& order, const QString& out) {
    return extract(in, order, out);
}

OpResult rotate(const QString& in, const QList<int>& pages, int degrees, const QString& out) {
    return guarded([&](OpResult& r) {
        if (degrees % 90 != 0) throw std::runtime_error("rotation must be a multiple of 90");
        auto q = load(in);
        auto all = QPDFPageDocumentHelper(*q).getAllPages();
        QList<int> sel = pages;
        if (sel.isEmpty()) for (int i = 0; i < int(all.size()); ++i) sel << i;
        for (int p : sel) {
            if (p < 0 || p >= int(all.size())) throw std::runtime_error("page index out of range");
            all[size_t(p)].rotatePage(degrees, true);
        }
        save(*q, out, r);
    });
}

OpResult copy(const QString& in, const QString& out) {
    return guarded([&](OpResult& r) { auto q = load(in); save(*q, out, r); });
}

QList<int> rotations(const QString& in, QString* error) {
    QList<int> res;
    try {
        auto q = load(in);
        for (auto& p : QPDFPageDocumentHelper(*q).getAllPages()) {
            auto rot = p.getAttribute("/Rotate", false);
            int v = rot.isInteger() ? int(rot.getIntValue()) : 0;
            res << (((v % 360) + 360) % 360);
        }
    } catch (std::exception& e) { if (error) *error = e.what(); }
    return res;
}

std::optional<QList<int>> parseRange(const QString& spec, int n) {
    QList<int> out;
    for (QString part : spec.split(',', Qt::SkipEmptyParts)) {
        part = part.trimmed();
        bool ok1 = true, ok2 = true;
        int a, b;
        if (int dash = part.indexOf('-'); dash >= 0) {
            a = part.left(dash).trimmed().toInt(&ok1);
            b = part.mid(dash + 1).trimmed().toInt(&ok2);
        } else { a = b = part.toInt(&ok1); }
        if (!ok1 || !ok2 || a < 1 || b < a || b > n) return std::nullopt;
        for (int i = a; i <= b; ++i) out << i - 1;
    }
    if (out.isEmpty()) return std::nullopt;
    return out;
}
}
