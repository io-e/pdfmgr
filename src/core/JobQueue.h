#pragma once
#include <QObject>
#include <QVariant>
#include <QThreadPool>
#include <functional>
#include <atomic>

namespace pdfcore {
// Runs engine work off the UI thread; results come back as queued signals.
// Render jobs carry a generation: bumpGeneration() makes in-flight renders stale;
// stale results are dropped and the abort flag aborts MuPDF via fz_cookie.
class JobQueue : public QObject {
    Q_OBJECT
public:
    using Work = std::function<QVariant()>;
    using RenderWork = std::function<QVariant(const std::atomic<bool>& abort)>;
    explicit JobQueue(QObject* parent = nullptr, int maxThreads = 0);
    ~JobQueue() override;
    quint64 submit(Work w);                          // generic job
    quint64 submitRender(quint64 generation, RenderWork w);
    quint64 generation() const { return m_generation.load(); }
    quint64 bumpGeneration();
    void waitForDone();
signals:
    void finished(quint64 id, QVariant result);
    void failed(quint64 id, QString error);
    void renderFinished(quint64 id, quint64 generation, QVariant result);
    void renderDiscarded(quint64 id, quint64 generation);
private:
    QThreadPool m_pool;
    std::atomic<quint64> m_nextId{1};
    std::atomic<quint64> m_generation{0};
};
}
