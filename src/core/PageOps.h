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
int pageCount(const QString& in, QString* error = nullptr);
OpResult merge(const QStringList& inputs, const QString& out);
// Progress callback: (units done, units total). Return false to cancel.
using Progress = std::function<bool(int done, int total)>;
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
OpResult rotate(const QString& in, const QList<int>& pages, int degrees, const QString& out);
// Rewrite (e.g. to save a temp copy for printing).
OpResult copy(const QString& in, const QString& out);
// Page /Rotate values (normalised 0..270), for tests/UI.
QList<int> rotations(const QString& in, QString* error = nullptr);
// Parse "1-3,5" (1-based) into 0-based indexes within [0,n).
std::optional<QList<int>> parseRange(const QString& spec, int pageCount);
}
}
