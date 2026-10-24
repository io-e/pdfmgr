#include "MainWindow.h"
#include "DocumentTab.h"
#include "PageOps.h"
#include "PrintDialog.h"
#include <QTabWidget>
#include <QToolBar>
#include <QMenuBar>
#include <QLineEdit>
#include <QStatusBar>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QDockWidget>
#include <QPlainTextEdit>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <QTime>
using namespace pdfcore;

MainWindow::MainWindow() {
    setWindowTitle("PDF Manager");
    resize(1200, 850);
    m_tabs = new QTabWidget; m_tabs->setTabsClosable(true); m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i) {
        auto* t = static_cast<DocumentTab*>(m_tabs->widget(i));
        if (t->isModified() && QMessageBox::question(this, tr("Close"), tr("Discard unsaved changes?")) != QMessageBox::Yes) return;
        m_tabs->removeTab(i); delete t;
    });
    m_jobLog = new QPlainTextEdit; m_jobLog->setReadOnly(true);
    auto* dock = new QDockWidget(tr("Jobs"), this); dock->setWidget(m_jobLog);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    connect(&m_print, &PrintService::jobStarted, this, [this](quint64 id, QString prog, QStringList args) {
        log(tr("Print #%1: %2 %3").arg(id).arg(prog, args.join(' ')));
    });
    connect(&m_print, &PrintService::jobFinished, this, [this](quint64 id, bool ok, int code, QString err) {
        log(tr("Print #%1 %2 (exit %3) %4").arg(id).arg(ok ? tr("done") : tr("FAILED")).arg(code).arg(err.trimmed()));
    });
    buildActions();
    log(tr("Print backend: %1").arg(m_print.backend().name()));
}

void MainWindow::log(const QString& s) { m_jobLog->appendPlainText(QTime::currentTime().toString("HH:mm:ss ") + s); }
DocumentTab* MainWindow::current() const { return qobject_cast<DocumentTab*>(m_tabs->currentWidget()); }

bool MainWindow::openFile(const QString& path) {
    QString err;
    auto* t = DocumentTab::open(path, &m_jobs, &err);
    if (!t) { QMessageBox::warning(this, tr("Open"), tr("Cannot open %1:\n%2").arg(path, err)); return false; }
    connect(t, &DocumentTab::status, statusBar(), [this](QString m) { statusBar()->showMessage(m); });
    connect(t, &DocumentTab::stateChanged, this, [this, t] {
        int i = m_tabs->indexOf(t);
        if (i >= 0) m_tabs->setTabText(i, QFileInfo(t->originalPath()).fileName() + (t->isModified() ? " *" : ""));
    });
    m_tabs->setCurrentIndex(m_tabs->addTab(t, QFileInfo(path).fileName()));
    return true;
}

void MainWindow::buildActions() {
    auto* tb = addToolBar(tr("Main"));
    auto* file = menuBar()->addMenu(tr("&File"));
    auto* view = menuBar()->addMenu(tr("&View"));
    auto* pages = menuBar()->addMenu(tr("&Pages"));
    auto add = [&](QMenu* m, const QString& text, QKeySequence ks, auto fn, bool toolbar = true) {
        auto* a = m->addAction(text); a->setShortcut(ks);
        connect(a, &QAction::triggered, this, fn);
        if (toolbar) tb->addAction(a);
        return a;
    };
    add(file, tr("Open…"), QKeySequence::Open, [this] {
        for (const auto& f : QFileDialog::getOpenFileNames(this, tr("Open PDF"), {}, tr("PDF (*.pdf)"))) openFile(f);
    });
    add(file, tr("Save"), QKeySequence::Save, [this] { save(false); });
    add(file, tr("Save as…"), QKeySequence::SaveAs, [this] { save(true); }, false);
    add(file, tr("Print…"), QKeySequence::Print, [this] { print(); });
    file->addSeparator();
    add(file, tr("Quit"), QKeySequence::Quit, [this] { close(); }, false);
    tb->addSeparator();
    add(view, tr("Previous"), QKeySequence(Qt::Key_PageUp), [this] { if (auto* t = current()) t->setPage(t->currentPage() - 1); });
    add(view, tr("Next"), QKeySequence(Qt::Key_PageDown), [this] { if (auto* t = current()) t->setPage(t->currentPage() + 1); });
    add(view, tr("Zoom in"), QKeySequence::ZoomIn, [this] { if (auto* t = current()) t->setZoom(t->zoom() * 1.25); });
    add(view, tr("Zoom out"), QKeySequence::ZoomOut, [this] { if (auto* t = current()) t->setZoom(t->zoom() / 1.25); });
    add(view, tr("100%"), QKeySequence(Qt::CTRL | Qt::Key_0), [this] { if (auto* t = current()) t->setZoom(1.0); });
    tb->addSeparator();
    add(pages, tr("Rotate left"), QKeySequence(Qt::CTRL | Qt::Key_L), [this] { rotate(-90); });
    add(pages, tr("Rotate right"), QKeySequence(Qt::CTRL | Qt::Key_R), [this] { rotate(90); });
    add(pages, tr("Move page up"), QKeySequence(Qt::CTRL | Qt::Key_Up), [this] { movePage(-1); });
    add(pages, tr("Move page down"), QKeySequence(Qt::CTRL | Qt::Key_Down), [this] { movePage(1); });
    add(pages, tr("Delete page"), QKeySequence::Delete, [this] { deletePage(); }, false);
    add(pages, tr("Extract pages…"), {}, [this] { extractPages(); }, false);
    add(pages, tr("Split…"), {}, [this] { splitDoc(); }, false);
    add(pages, tr("Merge with…"), {}, [this] { mergeWith(); }, false);
    tb->addSeparator();
    m_search = new QLineEdit; m_search->setPlaceholderText(tr("Search…")); m_search->setMaximumWidth(220);
    m_search->setClearButtonEnabled(true);
    tb->addWidget(m_search);
    connect(m_search, &QLineEdit::returnPressed, this, [this, last = std::make_shared<QString>()] {
        auto* t = current(); if (!t) return;
        if (*last == m_search->text()) t->findNext(); else { *last = m_search->text(); t->find(*last); }
    });
}

static QString tempOut() { return QDir::temp().filePath("pdfmgr-op-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".pdf"); }

void MainWindow::runOp(const std::function<OpResult(const QString&, const QString&)>& op, const QList<int>& affected) {
    auto* t = current(); if (!t) return;
    // MVP: page ops run synchronously (small docs); TODO move to JobQueue with progress.
    QString out = tempOut();
    OpResult r = op(t->workingPath(), out);
    QString err;
    if (!r.ok || !t->applyWorkingCopy(out, &err, affected)) {
        QFile::remove(out);
        QMessageBox::warning(this, tr("Operation failed"), r.ok ? err : r.error);
    }
}
void MainWindow::rotate(int deg) {
    auto* t = current(); if (!t) return;
    int p = t->currentPage();
    runOp([=](const QString& in, const QString& out) { return PageOps::rotate(in, {p}, deg, out); }, {p});
}
void MainWindow::movePage(int d) {
    auto* t = current(); if (!t) return;
    int p = t->currentPage(), n = t->pageCount(), q = p + d;
    if (q < 0 || q >= n) return;
    QList<int> order; for (int i = 0; i < n; ++i) order << i;
    order.swapItemsAt(p, q);
    runOp([=](const QString& in, const QString& out) { return PageOps::reorder(in, order, out); }, {p, q});
    t->setPage(q);
}
void MainWindow::deletePage() {
    auto* t = current(); if (!t || t->pageCount() < 2) return;
    QList<int> keep; for (int i = 0; i < t->pageCount(); ++i) if (i != t->currentPage()) keep << i;
    runOp([=](const QString& in, const QString& out) { return PageOps::extract(in, keep, out); });
}
void MainWindow::extractPages() {
    auto* t = current(); if (!t) return;
    QString spec = QInputDialog::getText(this, tr("Extract"), tr("Pages (e.g. 1-3,5):"));
    auto pages = PageOps::parseRange(spec, t->pageCount());
    if (!pages) { if (!spec.isEmpty()) QMessageBox::warning(this, tr("Extract"), tr("Invalid range")); return; }
    QString out = QFileDialog::getSaveFileName(this, tr("Save extracted pages"), {}, tr("PDF (*.pdf)"));
    if (out.isEmpty()) return;
    auto r = PageOps::extract(t->workingPath(), *pages, out);
    if (r.ok) openFile(out); else QMessageBox::warning(this, tr("Extract"), r.error);
}
void MainWindow::splitDoc() {
    auto* t = current(); if (!t) return;
    bool ok; int per = QInputDialog::getInt(this, tr("Split"), tr("Pages per file:"), 1, 1, t->pageCount(), 1, &ok);
    if (!ok) return;
    QString dir = QFileDialog::getExistingDirectory(this, tr("Output folder")); if (dir.isEmpty()) return;
    QString prefix = QDir(dir).filePath(QFileInfo(t->originalPath()).completeBaseName());
    auto r = PageOps::split(t->workingPath(), prefix, per);
    if (r.ok) log(tr("Split into %1 files in %2").arg(r.outputs.size()).arg(dir));
    else QMessageBox::warning(this, tr("Split"), r.error);
}
void MainWindow::mergeWith() {
    auto* t = current(); if (!t) return;
    QStringList others = QFileDialog::getOpenFileNames(this, tr("Append PDFs"), {}, tr("PDF (*.pdf)"));
    if (others.isEmpty()) return;
    runOp([=](const QString& in, const QString& out) { return PageOps::merge(QStringList{in} + others, out); });
}
void MainWindow::save(bool as) {
    auto* t = current(); if (!t) return;
    QString path = t->originalPath();
    if (as) { path = QFileDialog::getSaveFileName(this, tr("Save as"), path, tr("PDF (*.pdf)")); if (path.isEmpty()) return; }
    QString err;
    if (!t->saveTo(path, &err)) QMessageBox::warning(this, tr("Save"), err);
    else statusBar()->showMessage(tr("Saved %1").arg(path), 3000);
}
void MainWindow::print() {
    auto* t = current(); if (!t) return;
    PrintDialog d(t->pageCount(), this);
    if (d.exec() != QDialog::Accepted) return;
    PrintJob j = d.job();
    j.file = t->workingPath(); // PrintService copies it to a temp PDF first
    m_print.enqueue(j);
}
