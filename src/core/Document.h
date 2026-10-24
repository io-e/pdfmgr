#pragma once
#include <QString>
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QList>
#include <memory>
#include <mutex>
#include <atomic>
#include "CancelToken.h"
struct fz_document;

namespace pdfcore {
struct SearchHit { int page; QRectF rect; }; // rect in PDF points (72 dpi)

// Thread-safe MuPDF document. The fz_document is shared; every call runs in a
// cloned fz_context. Page load/display-list creation is serialised per document,
// rasterisation runs in parallel.
class Document {
public:
    static std::shared_ptr<Document> open(const QString& path, QString* error = nullptr);
    ~Document();
    QString path() const { return m_path; }
    int pageCount() const { return m_pageCount; }
    QSizeF pageSize(int page) const; // points
    // cancel: optional token; its fz_cookie is passed straight to MuPDF.
    QImage render(int page, double zoom, const CancelToken* cancel = nullptr) const;
    QImage thumbnail(int page, int maxSide = 160, const CancelToken* cancel = nullptr) const;
    QString pageText(int page) const;
    QList<SearchHit> search(const QString& needle, int maxHitsPerPage = 64) const;
private:
    Document() = default;
    QString m_path;
    fz_document* m_doc = nullptr;
    int m_pageCount = 0;
    mutable std::mutex m_docMutex;
};
}
