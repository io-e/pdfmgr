#pragma once
#include <QString>
#include <QByteArray>
namespace pdfcore {
// Writes data to a temp file next to `path`, then atomically replaces it (QSaveFile).
bool atomicWrite(const QString& path, const QByteArray& data, QString* error = nullptr);
}
