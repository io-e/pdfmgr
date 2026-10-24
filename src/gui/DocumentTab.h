#pragma once
#include <QWidget>
#include <memory>
#include <QSet>
#include <QVector>
#include <optional>
#include "CancelToken.h"
#include "Document.h"
#include "JobQueue.h"
class QListWidget; class QLabel; class QScrollArea; class QTimer;

// One open PDF. Edits are applied (QPDF) to a working copy; Save writes it back atomically.
class DocumentTab : public QWidget {
    Q_OBJECT
public:
    static DocumentTab* open(const QString& path, pdfcore::JobQueue* jobs, QString* error, QWidget* parent = nullptr);
    QString originalPath() const { return m_original; }
    QString workingPath() const { return m_working; }
    bool isModified() const { return m_modified; }
    int currentPage() const { return m_page; }
    int pageCount() const { return m_doc ? m_doc->pageCount() : 0; }
    double zoom() const { return m_zoom; }
    std::shared_ptr<pdfcore::Document> document() const { return m_doc; }

    void setPage(int p);
    void setZoom(double z);
    void find(const QString& text);
    void findNext();
    // Replace the working copy with an operation result and reload. Never loses
    // the current working copy: on any failure the tab keeps its document and
    // edits, `newFile` is left untouched and `error` is set.
    // `affectedPages`: pages whose thumbnails must be regenerated when the page
    // count is unchanged (empty = all).
    bool applyWorkingCopy(const QString& newFile, QString* error, const QList<int>& affectedPages = {});
    quint64 tabId() const { return m_tabId; }
    // One background task (page operation or search) per tab at a time.
    // While busy, conflicting actions must be disabled (see MainWindow).
    bool isBusy() const { return m_task.has_value(); }
    QString taskLabel() const { return m_task ? m_task->label : QString(); }
    pdfcore::CancelToken beginTask(const QString& label, quint64 jobId = 0);
    void setTaskJob(quint64 jobId) { if (m_task) m_task->job = jobId; }
    void endTask();
    void cancelTask(); // cooperative; the task's result is then discarded
    bool taskCanceled() const { return m_task && m_task->token.isCancelled(); }
    void reportProgress(int done, int total) { emit progress(done, total); }
    bool saveTo(const QString& path, QString* error);
    ~DocumentTab() override;
protected:
    void resizeEvent(QResizeEvent* e) override { QWidget::resizeEvent(e); scheduleThumbs(); }
signals:
    void stateChanged();
    void status(QString msg);
    void busyChanged(bool busy);
    void progress(int done, int total);
private:
    DocumentTab(pdfcore::JobQueue* jobs, QWidget* parent);
    bool load(const QString& file, QString* error, const QList<int>& affected = {}, bool keepThumbs = false);
    void requestRender();
    void resetThumbnails(const QList<int>& affected, bool keepThumbs);
    void scheduleThumbs();
    void requestVisibleThumbs();
    void releaseDocument();
    void onSearchDone(const QVariantMap& m);
    static constexpr int kThumbBatch = 8;       // pages per job
    static constexpr int kMaxThumbBatches = 3;  // queued/running batches per tab
    static constexpr int kThumbMargin = 6;      // pages prefetched around the viewport
    pdfcore::JobQueue* m_jobs;
    const quint64 m_tabId; // monotonic, never reused (unlike a pointer value)
    std::shared_ptr<pdfcore::Document> m_doc;
    QString m_original, m_working;
    bool m_modified = false;
    int m_page = 0;
    double m_zoom = 1.0;
    quint64 m_thumbGen = 0;
    QVector<bool> m_thumbReady;
    QSet<int> m_thumbQueued;
    int m_thumbBatches = 0;
    QTimer* m_thumbTimer;
    struct Task { QString label; pdfcore::CancelToken token; quint64 job = 0; };
    std::optional<Task> m_task;
    QList<pdfcore::SearchHit> m_hits;
    int m_hitIndex = -1;
    QListWidget* m_thumbs;
    QScrollArea* m_scroll;
    QLabel* m_view;
};
