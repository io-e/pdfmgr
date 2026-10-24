#pragma once
#include "IPrintBackend.h"
#include <QObject>
#include <QQueue>
#include <QPointer>
#include <memory>
class QProcess; class QTimer;

namespace pdfcore {
// Sequential print queue: one backend process per job, async, with timeout.
// Each job's source is first rewritten (QPDF) to a temp PDF, which is printed.
class PrintService : public QObject {
    Q_OBJECT
public:
    explicit PrintService(std::unique_ptr<IPrintBackend> backend = defaultBackend(), QObject* parent = nullptr);
    ~PrintService() override;
    static std::unique_ptr<IPrintBackend> defaultBackend(); // Sumatra on Windows, CUPS elsewhere
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    const IPrintBackend& backend() const { return *m_backend; }
    // `job.file` is the source document. Returns job id.
    quint64 enqueue(PrintJob job);
    int pending() const { return m_queue.size() + (m_proc ? 1 : 0); }
signals:
    void jobStarted(quint64 id, QString program, QStringList args);
    void jobFinished(quint64 id, bool ok, int exitCode, QString stderrText);
    void idle();
private:
    struct Entry { quint64 id; PrintJob job; };
    void startNext();
    void finishCurrent(bool ok, int code, const QString& err);
    std::unique_ptr<IPrintBackend> m_backend;
    QQueue<Entry> m_queue;
    QProcess* m_proc = nullptr;
    QTimer* m_timer = nullptr;
    Entry m_current{};
    QString m_currentTemp;
    quint64 m_nextId = 1;
    int m_timeoutMs = 120000;
};
}
