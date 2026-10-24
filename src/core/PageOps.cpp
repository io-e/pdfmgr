#include "PageOps.h"
#include "AtomicWrite.h"
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/Buffer.hh>
#include <memory>
#include <vector>
#include <stdexcept>

namespace pdfcore::PageOps {
namespace {
std::shared_ptr<QPDF> load(const QString& path) {
    auto q = std::make_shared<QPDF>();
    q->processFile(path.toLocal8Bit().constData()); // TODO(windows): use wide path / InputSource for Unicode
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
void save(QPDF& q, const QString& out, OpResult& r) {
    QString err;
    if (!atomicWrite(out, writeToMemory(q), &err)) throw std::runtime_error(err.toStdString());
    r.outputs << out;
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
        for (const auto& in : inputs) {
            srcs.push_back(load(in));
            for (auto& page : QPDFPageDocumentHelper(*srcs.back()).getAllPages()) dh.addPage(page, false);
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
