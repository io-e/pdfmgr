#include "AtomicWrite.h"
#include <QSaveFile>
namespace pdfcore {
bool atomicWrite(const QString& path, const QByteArray& data, QString* error) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = f.errorString(); return false; }
    if (f.write(data) != data.size()) { if (error) *error = f.errorString(); f.cancelWriting(); return false; }
    if (!f.commit()) { if (error) *error = f.errorString(); return false; }
    return true;
}
}
