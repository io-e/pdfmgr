#pragma once
#include <QMainWindow>
#include "JobQueue.h"
#include "PrintService.h"
#include "PageOps.h"
#include <functional>
class QTabWidget; class QLineEdit; class QPlainTextEdit; class DocumentTab;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    bool openFile(const QString& path);
    DocumentTab* current() const;
    QTabWidget* tabs() const { return m_tabs; }
private:
    void buildActions();
    void runOp(const std::function<pdfcore::OpResult(const QString& in, const QString& out)>& op, const QList<int>& affected = {});
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
};
