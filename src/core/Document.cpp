#include "Document.h"
#include "MuPdfContext.h"
#include <mupdf/fitz.h>
#include <vector>
#include <algorithm>

namespace pdfcore {

std::shared_ptr<Document> Document::open(const QString& path, QString* error) {
    ScopedClone ctx;
    std::shared_ptr<Document> d(new Document);
    d->m_path = path;
    QByteArray p = path.toUtf8();
    fz_document* doc = nullptr;
    int n = 0;
    fz_var(doc);
    fz_try(ctx) {
        doc = fz_open_document(ctx, p.constData());
        if (fz_needs_password(ctx, doc)) fz_throw(ctx, FZ_ERROR_GENERIC, "document is password protected");
        n = fz_count_pages(ctx, doc);
    }
    fz_catch(ctx) {
        if (error) *error = QString::fromUtf8(fz_caught_message(ctx));
        if (doc) fz_drop_document(ctx, doc);
        return nullptr;
    }
    d->m_doc = doc;
    d->m_pageCount = n;
    return d;
}

Document::~Document() {
    if (m_doc) { ScopedClone ctx; fz_drop_document(ctx, m_doc); }
}

QSizeF Document::pageSize(int page) const {
    if (page < 0 || page >= m_pageCount) return {};
    ScopedClone ctx;
    std::lock_guard g(m_docMutex);
    fz_rect r{};
    fz_page* pg = nullptr;
    fz_var(pg);
    fz_try(ctx) { pg = fz_load_page(ctx, m_doc, page); r = fz_bound_page(ctx, pg); }
    fz_always(ctx) { fz_drop_page(ctx, pg); }
    fz_catch(ctx) { return {}; }
    return QSizeF(r.x1 - r.x0, r.y1 - r.y0);
}

QImage Document::render(int page, double zoom, const CancelToken* cancel) const {
    if (page < 0 || page >= m_pageCount || zoom <= 0) return {};
    ScopedClone ctx;
    fz_display_list* list = nullptr;
    fz_rect bounds{};
    fz_page* pg = nullptr;
    {
        std::lock_guard g(m_docMutex);
        fz_var(pg);
        fz_try(ctx) {
            pg = fz_load_page(ctx, m_doc, page);
            bounds = fz_bound_page(ctx, pg);
            list = fz_new_display_list_from_page(ctx, pg);
        }
        fz_always(ctx) { fz_drop_page(ctx, pg); }
        fz_catch(ctx) { return {}; }
    }
    fz_cookie local{};
    fz_cookie* cookie = cancel ? static_cast<fz_cookie*>(cancel->nativeCookie()) : &local;
    if (cancel && cancel->isCancelled()) { fz_drop_display_list(ctx, list); return {}; }
    QImage img;
    fz_pixmap* pix = nullptr;
    fz_device* dev = nullptr;
    fz_var(pix); fz_var(dev);
    fz_try(ctx) {
        fz_matrix ctm = fz_scale(zoom, zoom);
        fz_irect ib = fz_round_rect(fz_transform_rect(bounds, ctm));
        pix = fz_new_pixmap_with_bbox(ctx, fz_device_rgb(ctx), ib, nullptr, 0);
        fz_clear_pixmap_with_value(ctx, pix, 0xff);
        dev = fz_new_draw_device(ctx, fz_identity, pix);
        fz_run_display_list(ctx, list, dev, ctm, fz_infinite_rect, cookie);
        fz_close_device(ctx, dev);
        if (!(cancel && cancel->isCancelled())) {
            int w = fz_pixmap_width(ctx, pix), h = fz_pixmap_height(ctx, pix);
            QImage tmp(fz_pixmap_samples(ctx, pix), w, h, fz_pixmap_stride(ctx, pix), QImage::Format_RGB888);
            img = tmp.copy();
        }
    }
    fz_always(ctx) { fz_drop_device(ctx, dev); fz_drop_pixmap(ctx, pix); fz_drop_display_list(ctx, list); }
    fz_catch(ctx) { img = QImage(); }
    return img;
}

QImage Document::thumbnail(int page, int maxSide, const CancelToken* cancel) const {
    QSizeF s = pageSize(page);
    if (s.isEmpty()) return {};
    double zoom = maxSide / std::max(s.width(), s.height());
    return render(page, zoom, cancel);
}

QString Document::pageText(int page) const {
    if (page < 0 || page >= m_pageCount) return {};
    ScopedClone ctx;
    std::lock_guard g(m_docMutex);
    QString out;
    fz_stext_page* st = nullptr; fz_buffer* buf = nullptr;
    fz_var(st); fz_var(buf);
    fz_try(ctx) {
        st = fz_new_stext_page_from_page_number(ctx, m_doc, page, nullptr);
        buf = fz_new_buffer_from_stext_page(ctx, st);
        unsigned char* data = nullptr;
        size_t len = fz_buffer_storage(ctx, buf, &data);
        out = QString::fromUtf8(reinterpret_cast<const char*>(data), qsizetype(len));
    }
    fz_always(ctx) { fz_drop_buffer(ctx, buf); fz_drop_stext_page(ctx, st); }
    fz_catch(ctx) { return {}; }
    return out;
}

QList<SearchHit> Document::search(const QString& needle, int maxHits, const CancelToken* cancel,
                                  const std::function<void(int, int)>& progress) const {
    QList<SearchHit> hits;
    if (needle.isEmpty()) return hits;
    ScopedClone ctx;
    QByteArray n = needle.toUtf8();
    std::vector<fz_quad> quads(maxHits);
    for (int p = 0; p < m_pageCount; ++p) {
        if (cancel && cancel->isCancelled()) break;
        if (progress && (p % 16 == 0)) progress(p, m_pageCount);
        std::lock_guard g(m_docMutex);
        int count = 0;
        fz_try(ctx) { count = fz_search_page_number(ctx, m_doc, p, n.constData(), nullptr, quads.data(), maxHits); }
        fz_catch(ctx) { count = 0; }
        for (int i = 0; i < count; ++i) {
            fz_rect r = fz_rect_from_quad(quads[i]);
            hits.push_back({p, QRectF(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0)});
        }
    }
    if (progress && !(cancel && cancel->isCancelled())) progress(m_pageCount, m_pageCount);
    return hits;
}
}
