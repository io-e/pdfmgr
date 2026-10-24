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

using namespace pdfcore;

DocumentTab::DocumentTab(JobQueue* jobs, QWidget* parent) : QWidget(parent), m_jobs(jobs) {
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
    connect(m_thumbs, &QListWidget::currentRowChanged, this, [this](int r) { if (r >= 0 && r != m_page) setPage(r); });
    connect(m_jobs, &JobQueue::renderFinished, this, [this](quint64, quint64 gen, QVariant v) {
        if (gen != m_jobs->generation()) return;
        auto m = v.toMap();
        if (m.value("tab").value<quintptr>() != quintptr(this) || m.value("page").toInt() != m_page) return;
        QImage img = m.value("img").value<QImage>();
        if (m_hitIndex >= 0 && m_hitIndex < m_hits.size() && m_hits[m_hitIndex].page == m_page) {
            QPainter p(&img);
            p.fillRect(QRectF(m_hits[m_hitIndex].rect.topLeft() * m_zoom, m_hits[m_hitIndex].rect.size() * m_zoom), QColor(255, 220, 0, 110));
        }
        m_view->setPixmap(QPixmap::fromImage(img));
    });
    connect(m_jobs, &JobQueue::finished, this, [this](quint64, QVariant v) {
        auto m = v.toMap();
        if (m.value("kind") != "thumb" || m.value("tab").value<quintptr>() != quintptr(this) || m.value("gen").toULongLong() != m_thumbGen) return;
        int p = m.value("page").toInt();
        if (auto* it = m_thumbs->item(p)) it->setIcon(QIcon(QPixmap::fromImage(m.value("img").value<QImage>())));
    });
}

DocumentTab::~DocumentTab() { m_jobs->bumpGeneration(); if (!m_working.isEmpty()) QFile::remove(m_working); }

DocumentTab* DocumentTab::open(const QString& path, JobQueue* jobs, QString* error, QWidget* parent) {
    auto* t = new DocumentTab(jobs, parent);
    t->m_original = QFileInfo(path).absoluteFilePath();
    t->m_working = QDir::temp().filePath("pdfmgr-work-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".pdf");
    if (!QFile::copy(path, t->m_working)) { if (error) *error = "cannot copy to working file"; t->m_working.clear(); delete t; return nullptr; }
    if (!t->load(t->m_working, error)) { delete t; return nullptr; }
    return t;
}

bool DocumentTab::load(const QString& file, QString* error) {
    auto d = Document::open(file, error);
    if (!d) return false;
    m_doc = d;
    m_hits.clear(); m_hitIndex = -1;
    m_page = std::clamp(m_page, 0, std::max(0, d->pageCount() - 1));
    loadThumbnails();
    requestRender();
    emit stateChanged();
    return true;
}

void DocumentTab::loadThumbnails() {
    ++m_thumbGen;
    m_thumbs->blockSignals(true);
    m_thumbs->clear();
    QPixmap blank(120, 160); blank.fill(Qt::lightGray);
    for (int i = 0; i < m_doc->pageCount(); ++i) m_thumbs->addItem(new QListWidgetItem(QIcon(blank), QString::number(i + 1)));
    m_thumbs->setCurrentRow(m_page);
    m_thumbs->blockSignals(false);
    auto doc = m_doc; quint64 gen = m_thumbGen; quintptr self = quintptr(this);
    for (int i = 0; i < doc->pageCount(); ++i)
        m_jobs->submit([doc, i, gen, self] {
            return QVariantMap{{"kind", "thumb"}, {"tab", QVariant::fromValue(self)}, {"gen", gen}, {"page", i}, {"img", doc->thumbnail(i, 150)}};
        });
}

void DocumentTab::requestRender() {
    if (!m_doc) return;
    quint64 gen = m_jobs->bumpGeneration(); // stale tiles get discarded
    auto doc = m_doc; int page = m_page; double z = m_zoom * devicePixelRatioF(); quintptr self = quintptr(this);
    double dpr = devicePixelRatioF();
    m_jobs->submitRender(gen, [doc, page, z, self, dpr](const std::atomic<bool>& abort) {
        QImage img = doc->render(page, z, &abort);
        img.setDevicePixelRatio(dpr);
        return QVariant(QVariantMap{{"tab", QVariant::fromValue(self)}, {"page", page}, {"img", img}});
    });
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

void DocumentTab::find(const QString& text) {
    if (!m_doc) return;
    m_hits = m_doc->search(text); // TODO: move to JobQueue for very large documents
    m_hitIndex = -1;
    if (m_hits.isEmpty()) { emit status(tr("No match for \"%1\"").arg(text)); requestRender(); return; }
    findNext();
}
void DocumentTab::findNext() {
    if (m_hits.isEmpty()) return;
    m_hitIndex = (m_hitIndex + 1) % m_hits.size();
    m_page = m_hits[m_hitIndex].page;
    setPage(m_page);
    emit status(tr("Match %1 / %2 (page %3)").arg(m_hitIndex + 1).arg(m_hits.size()).arg(m_page + 1));
}

bool DocumentTab::applyWorkingCopy(const QString& newFile, QString* error) {
    m_doc.reset();
    m_jobs->bumpGeneration();
    m_jobs->waitForDone(); // no worker may still hold the old document
    QFile::remove(m_working);
    if (!QFile::rename(newFile, m_working)) { if (error) *error = "cannot replace working file"; return false; }
    m_modified = true;
    return load(m_working, error);
}

bool DocumentTab::saveTo(const QString& path, QString* error) {
    QFile f(m_working);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    if (!atomicWrite(path, f.readAll(), error)) return false;
    m_original = path; m_modified = false; emit stateChanged();
    return true;
}
