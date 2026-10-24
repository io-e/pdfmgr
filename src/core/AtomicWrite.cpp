#include "AtomicWrite.h"
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QElapsedTimer>
#include <QUuid>
#include <algorithm>

namespace pdfcore {
bool atomicWrite(const QString& path, const QByteArray& data, QString* error) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = f.errorString(); return false; }
    if (f.write(data) != data.size()) { if (error) *error = f.errorString(); f.cancelWriting(); return false; }
    if (!f.commit()) { if (error) *error = f.errorString(); return false; }
    return true;
}

template <class F> static bool retry(F f, int timeoutMs) {
    QElapsedTimer t; t.start();
    int delay = 10;
    for (;;) {
        if (f()) return true;
        if (t.elapsed() >= timeoutMs) return false;
        QThread::msleep(unsigned(delay));
        delay = std::min(delay * 2, 200);
    }
}

bool removeWithRetry(const QString& path, int timeoutMs) {
    return retry([&] { return !QFileInfo::exists(path) || QFile::remove(path); }, timeoutMs);
}
bool renameWithRetry(const QString& from, const QString& to, int timeoutMs) {
    return retry([&] { return QFile::rename(from, to); }, timeoutMs);
}

bool replaceFileSafely(const QString& replacement, const QString& target, QString* error, int retryMs) {
    if (!QFileInfo::exists(replacement)) { if (error) *error = QStringLiteral("replacement file is missing: %1").arg(replacement); return false; }
    QString backup;
    if (QFileInfo::exists(target)) {
        backup = target + ".bak-" + QUuid::createUuid().toString(QUuid::Id128).left(8);
        if (!renameWithRetry(target, backup, retryMs)) {
            if (error) *error = QStringLiteral("cannot move %1 aside (file locked?)").arg(target);
            return false;
        }
    }
    if (!renameWithRetry(replacement, target, retryMs)) {
        if (!backup.isEmpty() && !renameWithRetry(backup, target, retryMs)) {
            if (error) *error = QStringLiteral("cannot replace %1; previous version kept at %2").arg(target, backup);
            return false;
        }
        if (error) *error = QStringLiteral("cannot replace %1 (file locked?); unchanged").arg(target);
        return false;
    }
    if (!backup.isEmpty()) removeWithRetry(backup, retryMs); // leftover backup is harmless
    return true;
}
}
