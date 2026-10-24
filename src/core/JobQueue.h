#pragma once
#include <QObject>
#include <QVariant>
#include <QThreadPool>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <vector>
#include "CancelToken.h"

namespace pdfcore {
// Runs engine work off the UI thread; results come back as queued signals.
//
// Every job may carry an `owner` id (e.g. a document tab). cancelOwner() makes
// that owner's queued jobs skip and cancels its running renders; waitForOwner()
// blocks only until *that owner's* jobs are done, never other tabs' work.
//
// Render jobs carry a generation: bumpGeneration() makes in-flight renders stale
// and cancels them through their fz_cookie (CancelToken); stale results are
// reported as renderDiscarded. No polling threads are involved.
class JobQueue : public QObject {
    Q_OBJECT
public:
    using Work = std::function<QVariant()>;
    using RenderWork = std::function<QVariant(const CancelToken& cancel)>;
    explicit JobQueue(QObject* parent = nullptr, int maxThreads = 0);
    ~JobQueue() override;
    quint64 submit(Work w, quint64 owner = 0);
    quint64 submitRender(quint64 generation, RenderWork w, quint64 owner = 0);
    quint64 generation() const { return m_generation.load(); }
    quint64 bumpGeneration();
    void cancelOwner(quint64 owner);
    void waitForOwner(quint64 owner);
    int pending(quint64 owner) const;
    void waitForDone(); // all owners; only for shutdown/tests
    static quint64 newOwnerId(); // monotonic, never reused
signals:
    void finished(quint64 id, QVariant result);
    void failed(quint64 id, QString error);
    void renderFinished(quint64 id, quint64 generation, QVariant result);
    void renderDiscarded(quint64 id, quint64 generation);
    void canceled(quint64 id);
private:
    struct Active { quint64 job, owner, gen; CancelToken token; };
    void end(quint64 owner);
    quint64 epoch(quint64 owner) const;
    QThreadPool m_pool;
    std::atomic<quint64> m_nextId{1};
    std::atomic<quint64> m_generation{0};
    mutable std::mutex m_mx;
    std::condition_variable m_cv;
    std::unordered_map<quint64, int> m_pending;     // owner -> queued+running jobs
    std::unordered_map<quint64, quint64> m_epoch;   // owner -> cancel epoch
    std::vector<Active> m_active;                   // running renders
};
}
