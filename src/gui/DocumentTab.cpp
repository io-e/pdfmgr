#include "DocumentTab.h"
#include "PageOps.h"
#include "AtomicWrite.h"
#include <QListWidget>
#include <QLabel>
#include <QScrollArea>
#include <QSplitter>
#include <QHBoxLayout>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QScrollBar>
#include <QTimer>
#include <QPointer>
#include <algorithm>

using namespace pdfcore;

DocumentTab::DocumentTab(JobQueue* jobs, QWidget* parent)
    : QWidget(parent), m_jobs(jobs), m_tabId(JobQueue::newOwnerId()) {
    m_thumbTimer = new QTimer(this); m_thumbTimer->setSingleShot(true); m_thumbTimer->setInterval(30);
    connect(m_thumbTimer, &QTimer::timeout, this, &DocumentTab::requestVisibleThumbs);
    m_thumbs = new QListWidget;
    m_thumbs->setViewMode(QListView::IconMode);
    m_thumbs->setFlow(QListView::TopToBottom);
    m_thumbs->setIconSize(QSize(120, 160));
    m_thumbs->setMovement(QListView::Static);
    m_thumbs->setFixedWidth(170);
    m_view = new QLabel; m_view->setAlignment(Qt::AlignCenter);
    m_scroll = new QScrollArea; m_scroll->setWidget(m_view); m_scroll->setWidgetResizable(true);
    m_scroll->setAlignment(Qt::AlignCenter);
    auto* split = new QSplitter; split->addWidget(m_thumbs); split->addWidget(m_scroll);
    split->setStretchFactor(1, 1);
    auto* l = new QHBoxLayout(this); l->setContentsMargins(0, 0, 0, 0); l->addWidget(split);
    connect(m_thumbs->verticalScrollBar(), &QScrollBar::valueChanged, this, &DocumentTab::scheduleThumbs);
    connect(m_thumbs, &QListWidget::currentRowChanged, this, [this](int r) { if (r >= 0 && r != m_page) setPage(r); });
    connect(m_jobs, &JobQueue::renderFinished, this, [this](quint64, quint64 gen, QVariant v) {
        if (gen != m_jobs->generation()) return;
        auto m = v.toMap();
        if (m.value("tab").toULongLong() != m_tabId || m.value("page").toInt() != m_page) return;
        QImage img = m.value("img").value<QImage>();
        if (m_hitIndex >= 0 && m_hitIndex < m_hits.size() && m_hits[m_hitIndex].page == m_page) {
            QPainter p(&img);
            p.fillRect(QRectF(m_hits[m_hitIndex].rect.topLeft() * m_zoom, m_hits[m_hitIndex].rect.size() * m_zoom), QColor(255, 220, 0, 110));
        }
        m_view->setPixmap(QPixmap::fromImage(img));
    });
    connect(m_jobs, &JobQueue::failed, this, [this](quint64 id, QString err) {
        if (!m_task || m_task->job != id || !m_task->label.startsWith(tr("Searching"))) return;
        endTask(); emit status(tr("Search failed: %1").arg(err));
    });
    connect(m_jobs, &JobQueue::finished, this, [this](quint64, QVariant v) {
        auto m = v.toMap();
        if (m.value("kind") == "search" && m.value("tab").toULongLong() == m_tabId) { onSearchDone(m); return; }
        if (m.value("kind") != "thumb" || m.value("tab").toULongLong() != m_tabId || m.value("gen").toULongLong() != m_thumbGen) return;
        --m_thumbBatches;
        const auto pages = m.value("pages").toList();
        const auto imgs = m.value("imgs").toList();
        for (int i = 0; i < pages.size() && i < imgs.size(); ++i) {
            int p = pages[i].toInt();
            m_thumbQueued.remove(p);
            if (p < 0 || p >= m_thumbReady.size()) continue;
            if (auto* it = m_thumbs->item(p)) it->setIcon(QIcon(QPixmap::fromImage(imgs[i].value<QImage>())));
            m_thumbReady[p] = true;
        }
        scheduleThumbs(); // next visible batch, if any
    });
}

// Cancels and waits for this tab's jobs only, then drops the MuPDF document so
// no handle on the working file remains (Windows cannot rename/delete open files).
void DocumentTab::releaseDocument() {
    if (m_task) m_task->token.cancel(); // a running search/page op stops early
    m_jobs->cancelOwner(m_tabId);
    m_jobs->waitForOwner(m_tabId);
    m_doc.reset();
    ++m_thumbGen; m_thumbQueued.clear(); m_thumbBatches = 0;
}

DocumentTab::~DocumentTab() {
    releaseDocument();
    if (!m_working.isEmpty()) removeWithRetry(m_working, 1000);
}

DocumentTab* DocumentTab::open(const QString& path, JobQueue* jobs, QString* error, QWidget* parent) {
    return open(path, jobs, QString(), error, nullptr, parent);
}

DocumentTab* DocumentTab::open(const QString& path, JobQueue* jobs, const QString& password,
                               QString* error, OpenError* code, QWidget* parent) {
    if (code) *code = OpenError::None;
    auto* t = new DocumentTab(jobs, parent);
    t->m_password = password;
    t->m_original = QFileInfo(path).absoluteFilePath();
    t->m_working = QDir::temp().filePath("pdfmgr-work-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".pdf");
    if (!QFile::copy(path, t->m_working)) { if (error) *error = "cannot copy to working file"; t->m_working.clear(); delete t; return nullptr; }
    if (!t->load(t->m_working, error, {}, false, code)) { delete t; return nullptr; }
    return t;
}

bool DocumentTab::load(const QString& file, QString* error, const QList<int>& affected, bool keepThumbs,
                       OpenError* code) {
    auto d = Document::open(file, m_password, error, code);
    if (!d) return false;
    m_password = d->password(); // dropped if the file no longer needs one
    keepThumbs = keepThumbs && m_thumbReady.size() == d->pageCount();
    m_doc = d;
    m_hits.clear(); m_hitIndex = -1;
    m_page = std::clamp(m_page, 0, std::max(0, d->pageCount() - 1));
    resetThumbnails(affected, keepThumbs);
    requestRender();
    emit stateChanged();
    return true;
}

void DocumentTab::resetThumbnails(const QList<int>& affected, bool keepThumbs) {
    ++m_thumbGen;
    m_thumbQueued.clear(); m_thumbBatches = 0;
    const int n = m_doc->pageCount();
    if (keepThumbs) {
        // Same page count: only the affected pages need new thumbnails.
        if (affected.isEmpty()) m_thumbReady.fill(false);
        for (int p : affected) if (p >= 0 && p < n) m_thumbReady[p] = false;
    } else {
        m_thumbs->blockSignals(true);
        m_thumbs->clear();
        QPixmap blank(120, 160); blank.fill(Qt::lightGray);
        for (int i = 0; i < n; ++i) m_thumbs->addItem(new QListWidgetItem(QIcon(blank), QString::number(i + 1)));
        m_thumbReady = QVector<bool>(n, false);
        m_thumbs->blockSignals(false);
    }
    m_thumbs->blockSignals(true); m_thumbs->setCurrentRow(m_page); m_thumbs->blockSignals(false);
    scheduleThumbs();
}

void DocumentTab::scheduleThumbs() { if (!m_thumbTimer->isActive()) m_thumbTimer->start(); }

// Renders only the thumbnails around the visible part of the list, in batches,
// with at most kMaxThumbBatches outstanding per tab. Scrolling requests more.
void DocumentTab::requestVisibleThumbs() {
    if (!m_doc) return;
    const int n = m_doc->pageCount();
    if (n == 0) return;
    auto* vp = m_thumbs->viewport();
    int first = m_thumbs->indexAt(QPoint(vp->width() / 2, 5)).row();
    int last = m_thumbs->indexAt(QPoint(vp->width() / 2, vp->height() - 5)).row();
    if (first < 0) first = std::max(0, m_thumbs->currentRow());
    if (last < first) last = std::min(n - 1, first + 8); // not laid out yet: guess one screen
    first = std::max(0, first - kThumbMargin);
    last = std::min(n - 1, last + kThumbMargin);
    QList<int> todo;
    for (int p = first; p <= last; ++p) if (!m_thumbReady[p] && !m_thumbQueued.contains(p)) todo << p;
    auto doc = m_doc; quint64 gen = m_thumbGen; quint64 tab = m_tabId;
    for (int i = 0; i < todo.size() && m_thumbBatches < kMaxThumbBatches; i += kThumbBatch) {
        QList<int> batch = todo.mid(i, kThumbBatch);
        for (int p : batch) m_thumbQueued.insert(p);
        ++m_thumbBatches;
        m_jobs->submit([doc, batch, gen, tab] {
            QVariantList pages, imgs;
            for (int p : batch) { pages << p; imgs << doc->thumbnail(p, 150); }
            return QVariant(QVariantMap{{"kind", "thumb"}, {"tab", tab}, {"gen", gen}, {"pages", pages}, {"imgs", imgs}});
        }, m_tabId);
    }
}

void DocumentTab::requestRender() {
    if (!m_doc) return;
    quint64 gen = m_jobs->bumpGeneration(); // stale tiles get discarded
    auto doc = m_doc; int page = m_page; double z = m_zoom * devicePixelRatioF(); quint64 tab = m_tabId;
    double dpr = devicePixelRatioF();
    m_jobs->submitRender(gen, [doc, page, z, tab, dpr](const CancelToken& cancel) {
        QImage img = doc->render(page, z, &cancel);
        img.setDevicePixelRatio(dpr);
        return QVariant(QVariantMap{{"tab", tab}, {"page", page}, {"img", img}});
    }, m_tabId);
    emit status(tr("Page %1 / %2 — zoom %3%").arg(m_page + 1).arg(pageCount()).arg(int(m_zoom * 100)));
}

void DocumentTab::setPage(int p) {
    if (!m_doc || p < 0 || p >= m_doc->pageCount()) return;
    m_page = p;
    m_thumbs->blockSignals(true); m_thumbs->setCurrentRow(p); m_thumbs->blockSignals(false);
    requestRender();
    emit stateChanged();
}
void DocumentTab::setZoom(double z) { m_zoom = std::clamp(z, 0.1, 8.0); requestRender(); emit stateChanged(); }

CancelToken DocumentTab::beginTask(const QString& label, quint64 jobId) {
    m_task = Task{label, CancelToken{}, jobId};
    emit status(label + QStringLiteral("…"));
    emit busyChanged(true);
    return m_task->token;
}
void DocumentTab::endTask() {
    if (!m_task) return;
    m_task.reset();
    emit busyChanged(false);
}
void DocumentTab::cancelTask() {
    if (!m_task) return;
    m_task->token.cancel();
    emit status(tr("Canceling %1…").arg(m_task->label));
}

// Search runs on the JobQueue; the UI stays responsive and can cancel it.
void DocumentTab::find(const QString& text) {
    if (!m_doc || isBusy() || text.isEmpty()) return;
    CancelToken token = beginTask(tr("Searching \"%1\"").arg(text));
    auto doc = m_doc; quint64 tab = m_tabId;
    QPointer<DocumentTab> self(this);
    quint64 id = m_jobs->submit([doc, text, token, tab, self] {
        auto hits = doc->search(text, 64, &token, [self](int d, int t) {
            // Posted to the tab's thread; dropped automatically if the tab is gone.
            if (self) QMetaObject::invokeMethod(self.data(), [self, d, t] { if (self) self->reportProgress(d, t); }, Qt::QueuedConnection);
        });
        QVariantList list;
        for (const auto& h : hits) list << QVariantList{h.page, h.rect};
        return QVariant(QVariantMap{{"kind", "search"}, {"tab", tab}, {"text", text}, {"hits", list}});
    }, m_tabId);
    setTaskJob(id);
}
void DocumentTab::onSearchDone(const QVariantMap& m) {
    const bool canceled = taskCanceled();
    endTask();
    if (canceled) { emit status(tr("Search canceled")); return; }
    m_hits.clear(); m_hitIndex = -1;
    for (const auto& v : m.value("hits").toList()) { auto l = v.toList(); m_hits.push_back({l.value(0).toInt(), l.value(1).toRectF()}); }
    if (m_hits.isEmpty()) { emit status(tr("No match for \"%1\"").arg(m.value("text").toString())); requestRender(); return; }
    findNext();
}
void DocumentTab::findNext() {
    if (m_hits.isEmpty()) return;
    m_hitIndex = (m_hitIndex + 1) % m_hits.size();
    m_page = m_hits[m_hitIndex].page;
    setPage(m_page);
    emit status(tr("Match %1 / %2 (page %3)").arg(m_hitIndex + 1).arg(m_hits.size()).arg(m_page + 1));
}

bool DocumentTab::applyWorkingCopy(const QString& newFile, QString* error, const QList<int>& affectedPages) {
    {   // Validate before touching anything; the probe's handle is closed right away.
        QString perr;
        auto probe = Document::open(newFile, m_password, &perr);
        if (!probe) { if (error) *error = tr("operation result is not a valid PDF: %1").arg(perr); return false; }
    }
    releaseDocument(); // this tab's jobs only; closes our handle on m_working
    QString err;
    if (!replaceFileSafely(newFile, m_working, &err)) {
        QString rerr;
        if (!load(m_working, &rerr)) err += "; " + tr("reload failed: %1").arg(rerr);
        if (error) *error = err;
        emit status(err);
        return false;
    }
    m_modified = true;
    return load(m_working, error, affectedPages, true);
}

bool DocumentTab::saveTo(const QString& path, QString* error) {
    QFile f(m_working);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    if (!atomicWrite(path, f.readAll(), error)) return false;
    m_original = path; m_modified = false; emit stateChanged();
    return true;
}
