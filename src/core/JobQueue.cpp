#include "JobQueue.h"
#include <QPointer>
#include <QMetaObject>
#include <algorithm>
#include <exception>

namespace pdfcore {
JobQueue::JobQueue(QObject* parent, int maxThreads) : QObject(parent) {
    if (maxThreads > 0) m_pool.setMaxThreadCount(maxThreads);
}
JobQueue::~JobQueue() {
    bumpGeneration();
    { std::lock_guard g(m_mx); for (auto& [o, e] : m_epoch) ++e; }
    m_pool.clear();
    m_pool.waitForDone();
}
quint64 JobQueue::newOwnerId() { static std::atomic<quint64> next{1}; return next++; }
void JobQueue::waitForDone() { m_pool.waitForDone(); }

quint64 JobQueue::bumpGeneration() {
    quint64 g = ++m_generation;
    std::lock_guard l(m_mx);
    for (auto& a : m_active) if (a.gen < g) a.token.cancel();
    return g;
}
void JobQueue::cancelOwner(quint64 owner) {
    std::lock_guard l(m_mx);
    ++m_epoch[owner];
    for (auto& a : m_active) if (a.owner == owner) a.token.cancel();
}
void JobQueue::waitForOwner(quint64 owner) {
    std::unique_lock l(m_mx);
    m_cv.wait(l, [&] { auto it = m_pending.find(owner); return it == m_pending.end() || it->second == 0; });
}
int JobQueue::pending(quint64 owner) const {
    std::lock_guard l(m_mx);
    auto it = m_pending.find(owner); return it == m_pending.end() ? 0 : it->second;
}
quint64 JobQueue::epoch(quint64 owner) const {
    auto it = m_epoch.find(owner); return it == m_epoch.end() ? 0 : it->second;
}
void JobQueue::end(quint64 owner) {
    { std::lock_guard l(m_mx); if (--m_pending[owner] <= 0) m_pending.erase(owner); }
    m_cv.notify_all();
}

quint64 JobQueue::submit(Work w, quint64 owner) {
    quint64 id = m_nextId++;
    quint64 ep; { std::lock_guard l(m_mx); ep = epoch(owner); ++m_pending[owner]; }
    QPointer<JobQueue> self(this);
    m_pool.start([this, self, id, owner, ep, w = std::move(w)] {
        bool skip; { std::lock_guard l(m_mx); skip = epoch(owner) != ep; }
        QVariant res; QString err; bool ok = true;
        if (!skip) {
            try { res = w(); } catch (std::exception& e) { ok = false; err = QString::fromUtf8(e.what()); }
            catch (...) { ok = false; err = QStringLiteral("unknown error"); }
        }
        end(owner);
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, id, res, err, ok, skip] {
            if (!self) return;
            if (skip) emit self->canceled(id);
            else if (ok) emit self->finished(id, res);
            else emit self->failed(id, err);
        }, Qt::QueuedConnection);
    });
    return id;
}

quint64 JobQueue::submitRender(quint64 gen, RenderWork w, quint64 owner) {
    quint64 id = m_nextId++;
    quint64 ep; { std::lock_guard l(m_mx); ep = epoch(owner); ++m_pending[owner]; }
    QPointer<JobQueue> self(this);
    m_pool.start([this, self, id, gen, owner, ep, w = std::move(w)] {
        CancelToken token;
        bool skip;
        {
            std::lock_guard l(m_mx);
            skip = m_generation.load() != gen || epoch(owner) != ep;
            if (!skip) m_active.push_back({id, owner, gen, token});
        }
        QVariant res;
        if (!skip) {
            try { res = w(token); } catch (...) {}
            std::lock_guard l(m_mx);
            m_active.erase(std::remove_if(m_active.begin(), m_active.end(), [id](const Active& a) { return a.job == id; }), m_active.end());
        }
        bool stale = skip || token.isCancelled() || m_generation.load() != gen;
        end(owner);
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, id, gen, res, stale] {
            if (!self) return;
            if (stale || self->generation() != gen) emit self->renderDiscarded(id, gen);
            else emit self->renderFinished(id, gen, res);
        }, Qt::QueuedConnection);
    });
    return id;
}
}
