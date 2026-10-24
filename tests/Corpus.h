#pragma once
#include <QString>
#include <QList>
#include <QStringList>
namespace corpus {
// One generated test file. Everything is produced by tests/Corpus.cpp at test
// time (no third-party content), see tests/corpus/README.md.
struct Entry {
    QString name;        // file name inside dir()
    QString category;    // forms, scanned, encrypted, large, unicode, sizes, malformed, rotation, linearized, minimal, base
    int pages = 0;       // expected page count
    QString password;    // user password needed to open (empty = none)
    QList<int> rotate;   // expected /Rotate per page (empty = all 0)
    bool mustRender = true; // page 0 must render to a non-empty image
    bool garbage = false;   // must fail to open cleanly
};
QList<Entry> entries();     // the full corpus (50 files), generated on first call
QString dir();              // directory holding the corpus
QString path(const QString& name);
// Writes the corpus into `outDir` (for manual testing on Windows); returns file count.
int writeTo(const QString& outDir);
}
