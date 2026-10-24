#pragma once
#include <QString>
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QRect>
#include <list>
#include <QList>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include "CancelToken.h"
struct fz_document;
struct fz_display_list;

namespace pdfcore {
struct SearchHit { int page; QRectF rect; }; // rect in PDF points (72 dpi)

// Why Document::open failed. NeedsPassword: the file is encrypted with a user
// password and none was given; WrongPassword: a password was given but rejected.
// Files protected by an owner password only (empty user password) open without
// any password.
enum class OpenError { None, NeedsPassword, WrongPassword, Other };

// Thread-safe MuPDF document. The fz_document is shared; every call runs in a
// cloned fz_context. Page load/display-list creation is serialised per document,
// rasterisation runs in parallel.
class Document {
public:
    static std::shared_ptr<Document> open(const QString& path, QString* error = nullptr);
    // `password` is tried with fz_authenticate_password when the file needs one.
    // It is kept in memory only (for QPDF page operations) and never logged.
    static std::shared_ptr<Document> open(const QString& path, const QString& password,
                                          QString* error, OpenError* code = nullptr);
    // The password that unlocked the document (empty if none was needed).
    QString password() const { return m_password; }
    bool isEncrypted() const { return m_encrypted; }
    ~Document();
    QString path() const { return m_path; }
    int pageCount() const { return m_pageCount; }
    QSizeF pageSize(int page) const; // points
    // cancel: optional token; its fz_cookie is passed straight to MuPDF.
    QImage render(int page, double zoom, const CancelToken* cancel = nullptr) const;
    // Size in device pixels of `page` rendered at `zoom` (what render() returns).
    QSize pageSizePx(int page, double zoom) const;
    // Renders only `tilePx` (device pixels, origin at the page's top-left) of the
    // page at `zoom`. Display lists of recently used pages are kept, so the tiles
    // of one page do not re-parse it. Returns a null image when canceled.
    QImage renderTile(int page, double zoom, const QRect& tilePx, const CancelToken* cancel = nullptr) const;
    QImage thumbnail(int page, int maxSide = 160, const CancelToken* cancel = nullptr) const;
    QString pageText(int page) const;
    // Searches every page. `cancel` stops early (partial hits are returned);
    // `progress(pagesDone, pageCount)` is called from the calling thread.
    QList<SearchHit> search(const QString& needle, int maxHitsPerPage = 64,
                            const CancelToken* cancel = nullptr,
                            const std::function<void(int, int)>& progress = {}) const;
private:
    Document() = default;
    QString m_path;
    QString m_password;
    bool m_encrypted = false;
    fz_document* m_doc = nullptr;
    int m_pageCount = 0;
    mutable std::mutex m_docMutex;
    // Small LRU of display lists (guarded by m_docMutex); lists are kept with
    // fz_keep_display_list while in use, so eviction never frees a list in use.
    struct ListEntry { int page; fz_display_list* list; float x0, y0, x1, y1; };
    mutable std::list<ListEntry> m_lists;
    static constexpr size_t kMaxLists = 4;
    fz_display_list* displayList(void* ctx, int page, float bounds[4]) const;
};
}
