#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <QByteArray>
#include <optional>
#include <functional>

namespace pdfcore {
// QPDF-based page operations. Each call opens its own QPDF instances (QPDF is
// not thread-safe), so calls are safe from any single worker thread.
// Pages are 0-based. All outputs are written atomically.
struct OpResult { bool ok = false; QString error; QStringList outputs; };

namespace PageOps {
// Password used to open encrypted inputs on the current thread (QPDF
// processInputSource). Inputs without a user password ignore it. RAII; never
// logged. Encrypted inputs keep their encryption in in-place outputs.
class ScopedPassword {
public:
    explicit ScopedPassword(const QString& password);
    ~ScopedPassword();
    ScopedPassword(const ScopedPassword&) = delete;
    ScopedPassword& operator=(const ScopedPassword&) = delete;
private:
    QByteArray m_prev;
};
int pageCount(const QString& in, QString* error = nullptr);
// Progress callback: (units done, units total). Return false to cancel.
using Progress = std::function<bool(int done, int total)>;
// Cancellable operations (merge, rotate): `progress` is polled between pages
// and while QPDF writes objects (units = pages + 100 write percent). On cancel
// the result is {ok=false, error="canceled"}, no partial output is left behind
// and the inputs (including an in-place `out`) are untouched.
OpResult merge(const QStringList& inputs, const QString& out, const Progress& progress = {});
// Split into chunks of `pagesPerFile`; files named <outPrefix>-<n>.pdf (1-based).
// Each output is written to disk (temp file + atomic rename) as soon as it is
// built, so memory stays bounded by one chunk. Progress counts output files.
// On error or cancel, outputs already written by this call are removed.
OpResult split(const QString& in, const QString& outPrefix, int pagesPerFile = 1,
               const Progress& progress = {});
OpResult extract(const QString& in, const QList<int>& pages, const QString& out);
// newOrder: permutation of 0..n-1 (or subset; missing pages are dropped).
OpResult reorder(const QString& in, const QList<int>& newOrder, const QString& out);
// Relative rotation in multiples of 90 applied via /Rotate. Empty pages = all.
OpResult rotate(const QString& in, const QList<int>& pages, int degrees, const QString& out,
                const Progress& progress = {});
// Rewrite (e.g. to save a temp copy for printing).
OpResult copy(const QString& in, const QString& out);
// Rewrite without encryption (temp copy for printing an unlocked file).
OpResult copyDecrypted(const QString& in, const QString& out);
// Page /Rotate values (normalised 0..270), for tests/UI.
QList<int> rotations(const QString& in, QString* error = nullptr);
// New page order after moving the pages `moving` (any order, duplicates
// ignored) so that they sit, in ascending order, before page `dropBefore` of
// the original numbering (n = after the last page). Used by thumbnail drag and
// drop; the result is a permutation of 0..n-1.
QList<int> moveOrder(int n, const QList<int>& moving, int dropBefore);
// Inserts all pages of `files` before page `beforePage` of `in` (n = append).
// Appending is a plain merge (forms and bookmarks kept); inserting in the
// middle is merge + reorder through a temp file next to `out`.
OpResult insertFiles(const QString& in, const QStringList& files, int beforePage, const QString& out);
// Parse "1-3,5" (1-based) into 0-based indexes within [0,n).
std::optional<QList<int>> parseRange(const QString& spec, int pageCount);
}
}
