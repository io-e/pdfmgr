#pragma once
#include <QString>
#include <QByteArray>
namespace pdfcore {
// Writes data to a temp file next to `path`, then atomically replaces it (QSaveFile).
bool atomicWrite(const QString& path, const QByteArray& data, QString* error = nullptr);

// Removes `path`, retrying for up to `timeoutMs` (Windows refuses to delete a
// file that another handle still has open, e.g. a viewer or antivirus scan).
// Returns true if the file no longer exists.
bool removeWithRetry(const QString& path, int timeoutMs = 2000);
// Renames with the same retry policy (destination must not exist).
bool renameWithRetry(const QString& from, const QString& to, int timeoutMs = 2000);

// Replaces `target` with `replacement` without ever losing `target`:
//   1. target -> backup, 2. replacement -> target, 3. delete backup.
// If step 2 fails, the backup is moved back so `target` is unchanged and
// `replacement` is left in place. The caller must have closed every handle on
// `target` first (required on Windows). `backupPath` receives the backup path
// when the backup could not be deleted afterwards (non-fatal).
bool replaceFileSafely(const QString& replacement, const QString& target, QString* error = nullptr,
                       int retryMs = 2000);
}
