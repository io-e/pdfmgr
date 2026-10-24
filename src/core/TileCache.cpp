#include "TileCache.h"
#include <QtMath>
#include <algorithm>

namespace pdfcore {
TileCache::TileCache(qint64 maxBytes) : m_max(std::max<qint64>(0, maxBytes)) {}
int TileCache::zoomKey(double zoom) { return qRound(zoom * 1000.0); }

QImage TileCache::get(const TileKey& k) {
    auto it = m_index.find(k);
    if (it == m_index.end()) return {};
    m_lru.splice(m_lru.begin(), m_lru, it->second);
    return it->second->img;
}
void TileCache::erase(std::list<Item>::iterator it) {
    m_bytes -= it->cost;
    m_index.erase(it->key);
    m_lru.erase(it);
}
void TileCache::insert(const TileKey& k, const QImage& img) {
    if (auto it = m_index.find(k); it != m_index.end()) erase(it->second);
    const qint64 c = cost(img);
    if (img.isNull() || c > m_max) return;
    m_lru.push_front({k, img, c});
    m_index[k] = m_lru.begin();
    m_bytes += c;
    evict();
}
void TileCache::evict() {
    while (m_bytes > m_max && !m_lru.empty()) { erase(std::prev(m_lru.end())); ++m_evictions; }
}
void TileCache::invalidate(quint64 docGen) {
    for (auto it = m_lru.begin(); it != m_lru.end();) {
        auto next = std::next(it);
        if (it->key.docGen == docGen) erase(it);
        it = next;
    }
}
void TileCache::invalidatePage(quint64 docGen, int page) {
    for (auto it = m_lru.begin(); it != m_lru.end();) {
        auto next = std::next(it);
        if (it->key.docGen == docGen && it->key.page == page) erase(it);
        it = next;
    }
}
void TileCache::clear() { m_lru.clear(); m_index.clear(); m_bytes = 0; }
void TileCache::setMaxBytes(qint64 b) { m_max = std::max<qint64>(0, b); evict(); }

namespace Tiles {
QRect rect(int tx, int ty, QSize page, int ts) {
    return QRect(tx * ts, ty * ts, ts, ts).intersected(QRect(QPoint(0, 0), page));
}
QList<QPoint> visible(QSize page, const QRect& vis, int ts) {
    QList<QPoint> out;
    const QRect r = vis.intersected(QRect(QPoint(0, 0), page));
    if (r.isEmpty() || ts <= 0) return out;
    for (int ty = r.top() / ts; ty <= r.bottom() / ts; ++ty)
        for (int tx = r.left() / ts; tx <= r.right() / ts; ++tx) out << QPoint(tx, ty);
    return out;
}
}

QList<TileKey> TilePlanner::plan(quint64 docGen, int page, double zoom, QSize pageSizePx, const QRect& visiblePx, int ts) {
    QList<TileKey> out;
    const int zk = TileCache::zoomKey(zoom);
    for (const QPoint& t : Tiles::visible(pageSizePx, visiblePx, ts)) {
        TileKey k{docGen, page, zk, t.x(), t.y()};
        if (m_cache->contains(k) || m_inFlight.contains(k)) continue;
        m_inFlight.insert(k);
        out << k;
    }
    m_submitted += quint64(out.size());
    return out;
}
void TilePlanner::done(const TileKey& k, const QImage& img, quint64 currentDocGen) {
    m_inFlight.remove(k);
    if (!img.isNull() && k.docGen == currentDocGen) m_cache->insert(k, img);
}
QList<TileKey> TilePlanner::stale(const QList<TileKey>& keep) const {
    QSet<TileKey> k(keep.begin(), keep.end());
    QList<TileKey> out;
    for (const auto& t : m_inFlight) if (!k.contains(t)) out << t;
    return out;
}
}
