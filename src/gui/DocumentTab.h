#pragma once
#include <QWidget>
#include <memory>
#include "Document.h"
#include "JobQueue.h"
class QListWidget; class QLabel; class QScrollArea;

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
    // Replace the working copy with an operation result and reload.
    bool applyWorkingCopy(const QString& newFile, QString* error);
    bool saveTo(const QString& path, QString* error);
    ~DocumentTab() override;
signals:
    void stateChanged();
    void status(QString msg);
private:
    DocumentTab(pdfcore::JobQueue* jobs, QWidget* parent);
    bool load(const QString& file, QString* error);
    void requestRender();
    void loadThumbnails();
    pdfcore::JobQueue* m_jobs;
    std::shared_ptr<pdfcore::Document> m_doc;
    QString m_original, m_working;
    bool m_modified = false;
    int m_page = 0;
    double m_zoom = 1.0;
    quint64 m_thumbGen = 0;
    QList<pdfcore::SearchHit> m_hits;
    int m_hitIndex = -1;
    QListWidget* m_thumbs;
    QScrollArea* m_scroll;
    QLabel* m_view;
};
