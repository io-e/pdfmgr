#pragma once
#include <QListWidget>
#include <QStringList>

// Thumbnail strip with drag and drop:
//  - dragging thumbnails inside the strip asks to move those pages
//    (pagesMoveRequested); the list itself is not changed, the document is
//    reordered and reloaded instead;
//  - dropping PDF files from a file manager asks to insert them at the drop
//    position (filesDropped).
class ThumbList : public QListWidget {
    Q_OBJECT
public:
    explicit ThumbList(QWidget* parent = nullptr);
    // Page index the drop at `pos` (viewport coordinates) inserts before.
    int dropRow(const QPoint& pos) const;
    static QStringList pdfFiles(const QMimeData* m);
signals:
    void pagesMoveRequested(QList<int> pages, int dropBefore);
    void filesDropped(QStringList files, int dropBefore);
protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
};
