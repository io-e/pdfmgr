#pragma once
#include <QMainWindow>
#include "JobQueue.h"
#include "PrintService.h"
#include "PageOps.h"
#include <functional>
class QTabWidget; class QLineEdit; class QPlainTextEdit; class QProgressBar; class QToolButton; class QAction; class DocumentTab;
#include <QHash>
#include "CancelToken.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
    bool openFile(const QString& path);
    DocumentTab* current() const;
    QTabWidget* tabs() const { return m_tabs; }
private:
    void buildActions();
public:
    // Background task body: runs on a JobQueue worker. `progress` returns false
    // once the user canceled.
    using TaskFn = std::function<pdfcore::OpResult(const pdfcore::PageOps::Progress& progress)>;
    enum class OpKind { ApplyToTab, OpenResult, Split };
    // Runs `fn` for tab `t` on the JobQueue (owner = tab id); the tab is busy
    // until the result has been handled. Returns false if the tab is busy.
    bool runTask(DocumentTab* t, const QString& label, OpKind kind, TaskFn fn,
                 const QString& out = {}, const QList<int>& affected = {}, int jumpTo = -1);
    DocumentTab* tabById(quint64 id) const;
    void cancelCurrentTask();
private:
    void runOp(const QString& label, const std::function<pdfcore::OpResult(const QString& in, const QString& out)>& op,
               const QList<int>& affected = {}, int jumpTo = -1);
    struct PendingOp { quint64 tab; OpKind kind; QString out; QList<int> affected; int jumpTo; QString label; };
    void finishTask(quint64 jobId, bool ok, const QString& error, const QStringList& outputs);
    void updateActions();
    void rotate(int deg);
    void movePage(int delta);
    void deletePage();
    void extractPages();
    void splitDoc();
    void mergeWith();
    void save(bool as);
    void print();
    void log(const QString& s);
    pdfcore::JobQueue m_jobs;
    pdfcore::PrintService m_print;
    QTabWidget* m_tabs;
    QLineEdit* m_search;
    QPlainTextEdit* m_jobLog;
    QProgressBar* m_progress;
    QToolButton* m_cancel;
    QList<QAction*> m_docActions; // disabled while the current tab is busy
    QHash<quint64, PendingOp> m_ops; // job id -> pending task
};
