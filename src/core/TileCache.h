#pragma once
#include <QImage>
#include <QRect>
#include <QSize>
#include <QList>
#include <QHash>
#include <QSet>
#include <list>
#include <unordered_map>

namespace pdfcore {
// A rendered tile is identified by the document generation (a new value every
// time a tab loads a file, so a rotated/edited working copy never reuses tiles),
// the page, the zoom (device pixels per point, quantised) and the tile column/row.
struct TileKey {
    quint64 docGen = 0;
    int page = 0;
    int zoomKey = 0; // qRound(zoom * 1000)
    int tx = 0, ty = 0;
    bool operator==(const TileKey& o) const {
        return docGen == o.docGen && page == o.page && zoomKey == o.zoomKey && tx == o.tx && ty == o.ty;
    }
};
inline size_t qHash(const TileKey& k, size_t seed = 0) noexcept {
    return ::qHashMulti(seed, k.docGen, k.page, k.zoomKey, k.tx, k.ty);
}
struct TileKeyHash { size_t operator()(const TileKey& k) const noexcept { return qHash(k); } };

// LRU cache of rendered tiles bounded by memory (bytes of image data).
// Not thread-safe: used from the GUI thread only (render jobs hand images back
// through queued signals).
class TileCache {
public:
    explicit TileCache(qint64 maxBytes = 256ll << 20);
    static int zoomKey(double zoom);
    // Returns a null image on a miss; a hit becomes most recently used.
    QImage get(const TileKey& k);
    bool contains(const TileKey& k) const { return m_index.count(k) != 0; }
    // Inserts (or replaces) a tile, then evicts least recently used tiles until
    // the cache fits in maxBytes. A tile larger than the whole cache is dropped.
    void insert(const TileKey& k, const QImage& img);
    void invalidate(quint64 docGen);           // all tiles of one document generation
    void invalidatePage(quint64 docGen, int page);
    void clear();
    void setMaxBytes(qint64 b);
    qint64 maxBytes() const { return m_max; }
    qint64 bytes() const { return m_bytes; }
    int count() const { return int(m_index.size()); }
    quint64 evictions() const { return m_evictions; }
    static qint64 cost(const QImage& img) { return img.sizeInBytes(); }
private:
    struct Item { TileKey key; QImage img; qint64 cost; };
    void evict();
    void erase(std::list<Item>::iterator it);
    std::list<Item> m_lru; // front = most recently used
    std::unordered_map<TileKey, std::list<Item>::iterator, TileKeyHash> m_index;
    qint64 m_max, m_bytes = 0;
    quint64 m_evictions = 0;
};

// Tile geometry: tiles of `tileSize` device pixels covering a page rendered at
// `pageSizePx` device pixels. The last row/column may be smaller.
namespace Tiles {
constexpr int kDefaultSize = 256;
QRect rect(int tx, int ty, QSize pageSizePx, int tileSize = kDefaultSize);
// Tiles intersecting `visiblePx` (page device-pixel coordinates), row-major.
QList<QPoint> visible(QSize pageSizePx, const QRect& visiblePx, int tileSize = kDefaultSize);
}

// Decides which visible tiles need a render job: not cached and not already in
// flight. Tracks in-flight tiles so scrolling back and forth never queues the
// same tile twice. GUI-thread only.
class TilePlanner {
public:
    explicit TilePlanner(TileCache* cache) : m_cache(cache) {}
    // Tiles to submit now; they are marked in flight.
    QList<TileKey> plan(quint64 docGen, int page, double zoom, QSize pageSizePx, const QRect& visiblePx,
                        int tileSize = Tiles::kDefaultSize);
    // A job finished (img may be null when canceled/failed): stores the tile if
    // its generation is still current and clears the in-flight mark.
    void done(const TileKey& k, const QImage& img, quint64 currentDocGen);
    // In-flight tiles not in `keep` (no longer visible); the caller cancels them.
    QList<TileKey> stale(const QList<TileKey>& keep) const;
    void forget(const TileKey& k) { m_inFlight.remove(k); }
    void reset() { m_inFlight.clear(); }
    int inFlight() const { return int(m_inFlight.size()); }
    quint64 submitted() const { return m_submitted; }
private:
    TileCache* m_cache;
    QSet<TileKey> m_inFlight;
    quint64 m_submitted = 0;
};
}
