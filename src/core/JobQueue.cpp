#include "JobQueue.h"
#include <QPointer>
#include <QMetaObject>
#include <thread>
#include <chrono>
#include <exception>

namespace pdfcore {
JobQueue::JobQueue(QObject* parent, int maxThreads) : QObject(parent) {
    if (maxThreads > 0) m_pool.setMaxThreadCount(maxThreads);
}
JobQueue::~JobQueue() { bumpGeneration(); m_pool.waitForDone(); }
void JobQueue::waitForDone() { m_pool.waitForDone(); }
quint64 JobQueue::bumpGeneration() { return ++m_generation; }

quint64 JobQueue::submit(Work w) {
    quint64 id = m_nextId++;
    QPointer<JobQueue> self(this);
    m_pool.start([self, id, w = std::move(w)] {
        QVariant res; QString err; bool ok = true;
        try { res = w(); } catch (std::exception& e) { ok = false; err = e.what(); }
        catch (...) { ok = false; err = QStringLiteral("unknown error"); }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, id, res, err, ok] {
            if (!self) return;
            if (ok) emit self->finished(id, res); else emit self->failed(id, err);
        }, Qt::QueuedConnection);
    });
    return id;
}

quint64 JobQueue::submitRender(quint64 gen, RenderWork w) {
    quint64 id = m_nextId++;
    QPointer<JobQueue> self(this);
    auto* genPtr = &m_generation;
    m_pool.start([self, id, gen, genPtr, w = std::move(w)] {
        if (genPtr->load() != gen) {
            if (self) QMetaObject::invokeMethod(self.data(), [self, id, gen] { if (self) emit self->renderDiscarded(id, gen); }, Qt::QueuedConnection);
            return;
        }
        std::atomic<bool> abort{false}, done{false};
        std::thread watch([&] { while (!done) { if (genPtr->load() != gen) { abort = true; break; }
                                               std::this_thread::sleep_for(std::chrono::milliseconds(5)); } });
        QVariant res;
        try { res = w(abort); } catch (...) {}
        done = true; watch.join();
        bool stale = abort.load() || genPtr->load() != gen;
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
