#include "MainWindow.h"
#include "DocumentTab.h"
#include "PageOps.h"
#include "PrintDialog.h"
#include "ThumbList.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
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
#include <QProgressBar>
#include <QPointer>
#include <algorithm>
#include <QToolButton>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include "Document.h"
#include "Version.h"
static constexpr int kPasswordAttempts = 3;
using pdfcore::OpenError;
using namespace pdfcore;

MainWindow::MainWindow() {
    setWindowTitle("PDF Manager");
    setAcceptDrops(true); // PDFs dropped from a file manager open in new tabs
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
        if (!ok) {
            QString msg;
            if (err.contains("timeout", Qt::CaseInsensitive))
                msg = tr("The print backend did not respond within the timeout period.\n"
                         "The printer may be offline or busy.");
            else if (err.contains("failed to start", Qt::CaseInsensitive))
                msg = tr("Could not start the print backend.\n"
                         "Please check that the application is correctly installed.");
            else if (code != 0 && !err.trimmed().isEmpty())
                msg = tr("The print backend exited with code %1:\n%2").arg(code).arg(err.trimmed());
            else if (code != 0)
                msg = tr("The print backend exited with code %1.\n"
                         "The printer may not be available.").arg(code);
            else
                msg = tr("Printing failed:\n%1").arg(err.trimmed());
            QMessageBox::warning(this, tr("Print failed"), msg);
        }
    });
    m_progress = new QProgressBar; m_progress->setMaximumWidth(180); m_progress->setTextVisible(true); m_progress->hide();
    m_cancel = new QToolButton; m_cancel->setText(tr("Cancel")); m_cancel->hide();
    statusBar()->addPermanentWidget(m_progress); statusBar()->addPermanentWidget(m_cancel);
    connect(m_cancel, &QToolButton::clicked, this, &MainWindow::cancelCurrentTask);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::updateActions);
    connect(&m_jobs, &JobQueue::finished, this, [this](quint64 id, QVariant v) {
        if (!m_ops.contains(id)) return;
        auto m = v.toMap();
        finishTask(id, m.value("ok").toBool(), m.value("error").toString(), m.value("outputs").toStringList());
    });
    connect(&m_jobs, &JobQueue::failed, this, [this](quint64 id, QString err) { if (m_ops.contains(id)) finishTask(id, false, err, {}); });
    connect(&m_jobs, &JobQueue::canceled, this, [this](quint64 id) { if (m_ops.contains(id)) finishTask(id, false, tr("canceled"), {}); });
    buildActions();
    updateActions();
    log(tr("Print backend: %1").arg(m_print.backend().name()));
}

void MainWindow::log(const QString& s) { m_jobLog->appendPlainText(QTime::currentTime().toString("HH:mm:ss ") + s); }
DocumentTab* MainWindow::current() const { return qobject_cast<DocumentTab*>(m_tabs->currentWidget()); }

bool MainWindow::openFile(const QString& path) {
    QString err, password;
    OpenError code = OpenError::None;
    DocumentTab* t = DocumentTab::open(path, &m_jobs, password, &err, &code);
    // Encrypted with a user password: masked prompt, up to kPasswordAttempts tries.
    for (int attempt = 0; !t && attempt < kPasswordAttempts
         && (code == OpenError::NeedsPassword || code == OpenError::WrongPassword); ++attempt) {
        QString prompt = tr("\"%1\" is password protected.\nPassword:").arg(QFileInfo(path).fileName());
        if (code == OpenError::WrongPassword)
            prompt = tr("Incorrect password (%1 of %2 attempts used).\n").arg(attempt).arg(kPasswordAttempts) + prompt;
        bool ok = false;
        password = QInputDialog::getText(this, tr("Password required"), prompt, QLineEdit::Password, {}, &ok);
        if (!ok) return false; // user canceled: no error box
        t = DocumentTab::open(path, &m_jobs, password, &err, &code);
    }
    password.fill(QChar(0)); password.clear();
    if (!t) {
        if (code == OpenError::WrongPassword || code == OpenError::NeedsPassword)
            err = tr("Incorrect password. The document was not opened.");
        QMessageBox::warning(this, tr("Open"), tr("Cannot open %1:\n%2").arg(path, err));
        return false;
    }
    connect(t, &DocumentTab::status, statusBar(), [this](QString m) { statusBar()->showMessage(m); });
    connect(t, &DocumentTab::stateChanged, this, [this, t] {
        int i = m_tabs->indexOf(t);
        if (i >= 0) m_tabs->setTabText(i, QFileInfo(t->originalPath()).fileName() + (t->isModified() ? " *" : ""));
    });
    connect(t, &DocumentTab::pagesMoveRequested, this, [this, t](QList<int> p, int before) { movePages(t, p, before); });
    connect(t, &DocumentTab::filesDropped, this, [this, t](QStringList f, int before) { insertFiles(t, f, before); });
    connect(t, &DocumentTab::busyChanged, this, [this, t] { if (t == current()) updateActions(); });
    connect(t, &DocumentTab::progress, this, [this, t](int d, int total) {
        if (t != current()) return;
        m_progress->setRange(0, std::max(total, 1)); m_progress->setValue(d);
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
    auto addDoc = [&](QMenu* m, const QString& text, QKeySequence ks, auto fn, bool toolbar = true) {
        auto* a = add(m, text, ks, fn, toolbar); m_docActions << a; return a;
    };
    add(file, tr("Open…"), QKeySequence::Open, [this] {
        for (const auto& f : QFileDialog::getOpenFileNames(this, tr("Open PDF"), {}, tr("PDF (*.pdf)"))) openFile(f);
    });
    addDoc(file, tr("Save"), QKeySequence::Save, [this] { save(false); });
    addDoc(file, tr("Save as…"), QKeySequence::SaveAs, [this] { save(true); }, false);
    addDoc(file, tr("Print…"), QKeySequence::Print, [this] { print(); });
    file->addSeparator();
    add(file, tr("Quit"), QKeySequence::Quit, [this] { close(); }, false);
    tb->addSeparator();
    add(view, tr("Previous"), QKeySequence(Qt::Key_PageUp), [this] { if (auto* t = current()) t->setPage(t->currentPage() - 1); });
    add(view, tr("Next"), QKeySequence(Qt::Key_PageDown), [this] { if (auto* t = current()) t->setPage(t->currentPage() + 1); });
    add(view, tr("Zoom in"), QKeySequence::ZoomIn, [this] { if (auto* t = current()) t->setZoom(t->zoom() * 1.25); });
    add(view, tr("Zoom out"), QKeySequence::ZoomOut, [this] { if (auto* t = current()) t->setZoom(t->zoom() / 1.25); });
    add(view, tr("100%"), QKeySequence(Qt::CTRL | Qt::Key_0), [this] { if (auto* t = current()) t->setZoom(1.0); });
    tb->addSeparator();
    addDoc(pages, tr("Rotate left"), QKeySequence(Qt::CTRL | Qt::Key_L), [this] { rotate(-90); });
    addDoc(pages, tr("Rotate right"), QKeySequence(Qt::CTRL | Qt::Key_R), [this] { rotate(90); });
    addDoc(pages, tr("Move page up"), QKeySequence(Qt::CTRL | Qt::Key_Up), [this] { movePage(-1); });
    addDoc(pages, tr("Move page down"), QKeySequence(Qt::CTRL | Qt::Key_Down), [this] { movePage(1); });
    addDoc(pages, tr("Delete page"), QKeySequence::Delete, [this] { deletePage(); }, false);
    addDoc(pages, tr("Extract pages…"), {}, [this] { extractPages(); }, false);
    addDoc(pages, tr("Split…"), {}, [this] { splitDoc(); }, false);
    addDoc(pages, tr("Merge with…"), {}, [this] { mergeWith(); }, false);
    tb->addSeparator();
    m_search = new QLineEdit; m_search->setPlaceholderText(tr("Search…")); m_search->setMaximumWidth(220);
    m_search->setClearButtonEnabled(true);
    tb->addWidget(m_search);
    auto* help = menuBar()->addMenu(tr("&Help"));
    add(help, tr("About…"), {}, [this] { showAbout(); }, false);
    auto* cancel = add(pages, tr("Cancel running task"), QKeySequence(Qt::Key_Escape), [this] { cancelCurrentTask(); }, false);
    Q_UNUSED(cancel);
    connect(m_search, &QLineEdit::returnPressed, this, [this, last = std::make_shared<QString>()] {
        auto* t = current(); if (!t) return;
        if (*last == m_search->text()) t->findNext(); else { *last = m_search->text(); t->find(*last); }
    });
}


DocumentTab* MainWindow::tabById(quint64 id) const {
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto* t = qobject_cast<DocumentTab*>(m_tabs->widget(i)); t && t->tabId() == id) return t;
    return nullptr;
}

void MainWindow::updateActions() {
    auto* t = current();
    const bool busy = t && t->isBusy();
    for (auto* a : std::as_const(m_docActions)) a->setEnabled(t && !busy);
    m_search->setEnabled(t && !busy);
    m_progress->setVisible(busy); m_cancel->setVisible(busy);
    if (busy) { m_progress->setRange(0, 0); m_progress->setFormat(t->taskLabel() + " %p%"); }
}

void MainWindow::cancelCurrentTask() { if (auto* t = current(); t && t->isBusy()) t->cancelTask(); }

bool MainWindow::runTask(DocumentTab* t, const QString& label, OpKind kind, TaskFn fn,
                         const QString& out, const QList<int>& affected, int jumpTo) {
    if (!t || t->isBusy()) return false;
    CancelToken token = t->beginTask(label);
    const quint64 tabId = t->tabId();
    QPointer<MainWindow> self(this);
    PageOps::Progress progress = [self, tabId, token](int d, int total) {
        if (self) QMetaObject::invokeMethod(self.data(), [self, tabId, d, total] {
            if (!self) return;
            if (auto* tab = self->tabById(tabId)) tab->reportProgress(d, total);
        }, Qt::QueuedConnection);
        return !token.isCancelled();
    };
    const QString password = t->password();
    quint64 id = m_jobs.submit([fn, progress, token, password] {
        if (token.isCancelled()) return QVariant(QVariantMap{{"ok", false}, {"error", QStringLiteral("canceled")}});
        PageOps::ScopedPassword pw(password); // unlocks the tab's encrypted working copy for QPDF
        OpResult r = fn(progress);
        return QVariant(QVariantMap{{"ok", r.ok}, {"error", r.error}, {"outputs", r.outputs}});
    }, tabId);
    t->setTaskJob(id);
    m_ops.insert(id, PendingOp{tabId, kind, out, affected, jumpTo, label});
    return true;
}

void MainWindow::finishTask(quint64 jobId, bool ok, const QString& error, const QStringList& outputs) {
    PendingOp op = m_ops.take(jobId);
    DocumentTab* t = tabById(op.tab);
    const bool canceled = !t || t->taskCanceled();
    if (t) t->endTask();
    auto discard = [&] {
        if (op.kind == OpKind::ApplyToTab && !op.out.isEmpty()) QFile::remove(op.out);
        // Split removes its own partial outputs on cancel/error.
    };
    if (canceled) {
        discard();
        if (op.kind == OpKind::Split || op.kind == OpKind::OpenResult) for (const auto& f : outputs) QFile::remove(f);
        if (t) statusBar()->showMessage(tr("%1 canceled").arg(op.label), 4000);
        return;
    }
    if (!ok) { discard(); QMessageBox::warning(this, tr("Operation failed"), error); return; }
    switch (op.kind) {
    case OpKind::ApplyToTab: {
        QString err;
        if (!t->applyWorkingCopy(op.out, &err, op.affected)) { QFile::remove(op.out); QMessageBox::warning(this, tr("Operation failed"), err); return; }
        if (op.jumpTo >= 0) t->setPage(op.jumpTo);
        statusBar()->showMessage(tr("%1 done").arg(op.label), 3000);
        break;
    }
    case OpKind::OpenResult: if (!op.out.isEmpty()) openFile(op.out); break;
    case OpKind::Split: log(tr("Split into %1 files: %2").arg(outputs.size()).arg(QFileInfo(op.out).absolutePath())); break;
    }
}

static QString tempOut() { return QDir::temp().filePath("pdfmgr-op-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".pdf"); }

void MainWindow::runOp(const QString& label, const CancellableOp& op, const QList<int>& affected, int jumpTo) {
    auto* t = current(); if (!t) return;
    const QString in = t->workingPath(), out = tempOut();
    runTask(t, label, OpKind::ApplyToTab, [op, in, out](const PageOps::Progress& p) { return op(in, out, p); }, out, affected, jumpTo);
}
void MainWindow::rotate(int deg) {
    auto* t = current(); if (!t) return;
    int p = t->currentPage();
    runOp(tr("Rotating"), [=](const QString& in, const QString& out, const PageOps::Progress& pr) { return PageOps::rotate(in, {p}, deg, out, pr); }, {p});
}
void MainWindow::movePage(int d) {
    auto* t = current(); if (!t) return;
    int p = t->currentPage(), n = t->pageCount(), q = p + d;
    if (q < 0 || q >= n) return;
    QList<int> order; for (int i = 0; i < n; ++i) order << i;
    order.swapItemsAt(p, q);
    runOp(tr("Moving page"), [=](const QString& in, const QString& out, const PageOps::Progress&) { return PageOps::reorder(in, order, out); }, {p, q}, q);
}
void MainWindow::deletePage() {
    auto* t = current(); if (!t || t->pageCount() < 2) return;
    QList<int> keep; for (int i = 0; i < t->pageCount(); ++i) if (i != t->currentPage()) keep << i;
    runOp(tr("Deleting page"), [=](const QString& in, const QString& out, const PageOps::Progress&) { return PageOps::extract(in, keep, out); });
}
void MainWindow::extractPages() {
    auto* t = current(); if (!t) return;
    QString spec = QInputDialog::getText(this, tr("Extract"), tr("Pages (e.g. 1-3,5):"));
    auto pages = PageOps::parseRange(spec, t->pageCount());
    if (!pages) { if (!spec.isEmpty()) QMessageBox::warning(this, tr("Extract"), tr("Invalid range")); return; }
    QString out = QFileDialog::getSaveFileName(this, tr("Save extracted pages"), {}, tr("PDF (*.pdf)"));
    if (out.isEmpty()) return;
    const QString in = t->workingPath(); const QList<int> sel = *pages;
    runTask(t, tr("Extracting pages"), OpKind::OpenResult, [in, sel, out](const PageOps::Progress&) { return PageOps::extract(in, sel, out); }, out);
}
void MainWindow::splitDoc() {
    auto* t = current(); if (!t) return;
    bool ok; int per = QInputDialog::getInt(this, tr("Split"), tr("Pages per file:"), 1, 1, t->pageCount(), 1, &ok);
    if (!ok) return;
    QString dir = QFileDialog::getExistingDirectory(this, tr("Output folder")); if (dir.isEmpty()) return;
    QString prefix = QDir(dir).filePath(QFileInfo(t->originalPath()).completeBaseName());
    const QString in = t->workingPath();
    runTask(t, tr("Splitting"), OpKind::Split, [in, prefix, per](const PageOps::Progress& p) { return PageOps::split(in, prefix, per, p); }, prefix);
}
void MainWindow::mergeWith() {
    auto* t = current(); if (!t) return;
    QStringList others = QFileDialog::getOpenFileNames(this, tr("Append PDFs"), {}, tr("PDF (*.pdf)"));
    if (others.isEmpty()) return;
    runOp(tr("Merging"), [=](const QString& in, const QString& out, const PageOps::Progress& pr) { return PageOps::merge(QStringList{in} + others, out, pr); });
}
bool MainWindow::movePages(DocumentTab* t, const QList<int>& pages, int dropBefore) {
    if (!t || t->isBusy() || pages.isEmpty()) return false;
    const int n = t->pageCount();
    const QList<int> order = PageOps::moveOrder(n, pages, dropBefore);
    QList<int> identity; for (int i = 0; i < n; ++i) identity << i;
    if (order == identity) return false; // dropped where they already are
    QList<int> affected;
    for (int i = 0; i < n; ++i) if (order[i] != i) affected << i;
    const int jumpTo = int(order.indexOf(std::clamp(pages.value(0), 0, n - 1)));
    const QString in = t->workingPath(), out = QDir::temp().filePath("pdfmgr-op-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".pdf");
    return runTask(t, tr("Moving pages"), OpKind::ApplyToTab,
                   [in, order, out](const PageOps::Progress&) { return PageOps::reorder(in, order, out); }, out, affected, jumpTo);
}
bool MainWindow::insertFiles(DocumentTab* t, const QStringList& files, int dropBefore) {
    if (!t || t->isBusy() || files.isEmpty()) return false;
    const QString in = t->workingPath(), out = QDir::temp().filePath("pdfmgr-op-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".pdf");
    const int before = dropBefore >= t->pageCount() ? -1 : dropBefore;
    return runTask(t, tr("Inserting %n file(s)", nullptr, int(files.size())), OpKind::ApplyToTab,
                   [in, files, before, out](const PageOps::Progress&) { return PageOps::insertFiles(in, files, before, out); },
                   out, {}, std::max(0, dropBefore));
}
// PDFs dropped anywhere else in the window (tab bar, page view, empty window)
// open in new tabs. Drops on a thumbnail strip are handled by ThumbList.
void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (!ThumbList::pdfFiles(e->mimeData()).isEmpty()) e->acceptProposedAction(); else e->ignore();
}
void MainWindow::dropEvent(QDropEvent* e) {
    const QStringList files = ThumbList::pdfFiles(e->mimeData());
    if (files.isEmpty()) { e->ignore(); return; }
    e->acceptProposedAction();
    for (const auto& f : files) openFile(f);
}

void MainWindow::save(bool as) {
    auto* t = current(); if (!t || t->isBusy()) return;
    QString path = t->originalPath();
    if (as) { path = QFileDialog::getSaveFileName(this, tr("Save as"), path, tr("PDF (*.pdf)")); if (path.isEmpty()) return; }
    QString err;
    if (!t->saveTo(path, &err)) QMessageBox::warning(this, tr("Save"), err);
    else statusBar()->showMessage(tr("Saved %1").arg(path), 3000);
}
void MainWindow::print() {
    auto* t = current(); if (!t || t->isBusy()) return;
    PrintDialog d(t->pageCount(), this);
    if (d.exec() != QDialog::Accepted) return;
    PrintJob j = d.job();
    j.file = t->workingPath(); // PrintService copies it to a temp PDF first
    j.password = t->password();
    m_print.enqueue(j);
}

void MainWindow::showAbout() {
    QString text = tr(
        "<h3>PDF Manager %1</h3>"
        "<p>Commit: %2<br>Built: %3</p>"
        "<table>"
        "<tr><td>Qt:</td><td>%4</td></tr>"
        "<tr><td>MuPDF:</td><td>%5</td></tr>"
        "<tr><td>QPDF:</td><td>%6</td></tr>"
        "<tr><td>Print backend:</td><td>%7</td></tr>"
        "</table>"
        "<p>Licensed under AGPLv3.</p>")
        .arg(QStringLiteral(PDFMGR_VERSION), QStringLiteral(PDFMGR_GIT_HASH),
             QStringLiteral(PDFMGR_BUILD_DATE), qVersion(),
             pdfcore::mupdfVersion(), pdfcore::qpdfVersion(),
             m_print.backend().name());
    QMessageBox::about(this, tr("About PDF Manager"), text);
}

// Tabs are child widgets, which ~QWidget would delete only after the members
// (m_jobs, m_print) are gone; their destructors use m_jobs, so delete them first.
MainWindow::~MainWindow() {
    while (tabs()->count() > 0) delete tabs()->widget(0);
}
