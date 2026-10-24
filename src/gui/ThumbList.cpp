#include "ThumbList.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QUrl>
#include <algorithm>

ThumbList::ThumbList(QWidget* parent) : QListWidget(parent) {
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
}

QStringList ThumbList::pdfFiles(const QMimeData* m) {
    QStringList out;
    if (!m || !m->hasUrls()) return out;
    for (const QUrl& u : m->urls()) {
        if (!u.isLocalFile()) continue;
        const QString f = u.toLocalFile();
        if (QFileInfo(f).isFile() && f.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) out << f;
    }
    return out;
}

int ThumbList::dropRow(const QPoint& pos) const {
    const QModelIndex idx = indexAt(pos);
    if (!idx.isValid()) {
        // Below the last item (or empty list): append; above the first: prepend.
        if (count() > 0 && pos.y() < visualItemRect(item(0)).top()) return 0;
        return count();
    }
    const QRect r = visualRect(idx);
    return pos.y() < r.center().y() ? idx.row() : idx.row() + 1;
}

void ThumbList::dragEnterEvent(QDragEnterEvent* e) {
    if (e->source() == this || !pdfFiles(e->mimeData()).isEmpty()) {
        e->setDropAction(e->source() == this ? Qt::MoveAction : Qt::CopyAction);
        e->accept();
        QListWidget::dragEnterEvent(e);
        e->accept();
    } else e->ignore();
}
void ThumbList::dragMoveEvent(QDragMoveEvent* e) {
    if (e->source() == this || !pdfFiles(e->mimeData()).isEmpty()) {
        QListWidget::dragMoveEvent(e);
        e->setDropAction(e->source() == this ? Qt::MoveAction : Qt::CopyAction);
        e->accept();
    } else e->ignore();
}
void ThumbList::dropEvent(QDropEvent* e) {
    const int row = dropRow(e->position().toPoint());
    if (e->source() == this) {
        QList<int> pages;
        for (auto* it : selectedItems()) pages << this->row(it);
        std::sort(pages.begin(), pages.end());
        // Never let QListWidget move the items itself: the document is reordered
        // by QPDF and the list rebuilt from it.
        e->setDropAction(Qt::IgnoreAction);
        e->accept();
        if (!pages.isEmpty()) emit pagesMoveRequested(pages, row);
        return;
    }
    const QStringList files = pdfFiles(e->mimeData());
    if (files.isEmpty()) { e->ignore(); return; }
    e->setDropAction(Qt::CopyAction);
    e->accept();
    emit filesDropped(files, row);
}
