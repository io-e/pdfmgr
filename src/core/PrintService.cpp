#include "PrintService.h"
#include "PageOps.h"
#include "CupsBackend.h"
#include "SumatraBackend.h"
#include <QProcess>
#include <QTimer>
#include <QTemporaryFile>
#include <QDir>
#include <QFile>

namespace pdfcore {
std::unique_ptr<IPrintBackend> PrintService::defaultBackend() {
#ifdef Q_OS_WIN
    return std::make_unique<SumatraBackend>();
#else
    return std::make_unique<CupsBackend>();
#endif
}
PrintService::PrintService(std::unique_ptr<IPrintBackend> b, QObject* parent)
    : QObject(parent), m_backend(std::move(b)) {}
PrintService::~PrintService() {
    if (m_proc) { m_proc->disconnect(this); m_proc->kill(); m_proc->waitForFinished(2000); }
    if (!m_currentTemp.isEmpty()) QFile::remove(m_currentTemp);
}
quint64 PrintService::enqueue(PrintJob job) {
    quint64 id = m_nextId++;
    m_queue.enqueue({id, std::move(job)});
    if (!m_proc) QTimer::singleShot(0, this, &PrintService::startNext);
    return id;
}
void PrintService::startNext() {
    if (m_proc) return;
    if (m_queue.isEmpty()) { emit idle(); return; }
    m_current = m_queue.dequeue();
    // 1. Save the document state to a temp PDF (QPDF write).
    QString tmpPath;
    {
        QTemporaryFile t(QDir::temp().filePath("pdfmgr-print-XXXXXX.pdf"));
        t.setAutoRemove(false);
        if (!t.open()) { finishCurrent(false, -1, "cannot create temp file"); return; }
        tmpPath = t.fileName();
    }
    auto r = PageOps::copy(m_current.job.file, tmpPath);
    if (!r.ok) { QFile::remove(tmpPath); finishCurrent(false, -1, "temp save failed: " + r.error); return; }
    m_currentTemp = tmpPath;
    PrintJob j = m_current.job; j.file = tmpPath;
    // 2. Launch backend asynchronously.
    m_proc = new QProcess(this);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (!m_proc) return;
        m_proc->disconnect(this);
        m_proc->kill(); m_proc->waitForFinished(2000);
        finishCurrent(false, -1, QStringLiteral("timeout after %1 ms").arg(m_timeoutMs));
    });
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        QString err = QString::fromLocal8Bit(m_proc->readAllStandardError());
        finishCurrent(st == QProcess::NormalExit && code == 0, code, err);
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) finishCurrent(false, -1, "failed to start " + m_backend->program() + ": " + m_proc->errorString());
    });
    QStringList args = m_backend->arguments(j);
    emit jobStarted(m_current.id, m_backend->program(), args);
    m_timer->start(m_timeoutMs);
    m_proc->start(m_backend->program(), args);
}
void PrintService::finishCurrent(bool ok, int code, const QString& err) {
    if (m_timer) { m_timer->stop(); m_timer->deleteLater(); m_timer = nullptr; }
    if (m_proc) { m_proc->disconnect(this); m_proc->deleteLater(); m_proc = nullptr; }
    if (!m_currentTemp.isEmpty()) { QFile::remove(m_currentTemp); m_currentTemp.clear(); }
    emit jobFinished(m_current.id, ok, code, err);
    QTimer::singleShot(0, this, &PrintService::startNext);
}
}
