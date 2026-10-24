#pragma once
#include <QString>
#include <QStringList>
#include <QSizeF>
namespace fixtures {
// Writes a valid PDF with `pages` pages; page i shows "Page <i+1> <word>".
void writePdf(const QString& path, int pages, const QString& word = "Hello", QSizeF size = {595, 842});
QString dir();                         // temp corpus dir, created once
QString path(const QString& name);     // corpus file (generated on first use)
QString tmpFile(const QString& name);  // writable scratch path
void writeFormPdf(const QString& path, const QString& field, const QString& value);
enum class Crypt { AES256, AES128, RC4_128 };
// Re-writes `in` encrypted. Empty user password = owner-password-only file.
void writeEncrypted(const QString& in, const QString& out, const char* user, const char* owner, Crypt c = Crypt::AES256);
bool isEncrypted(const QString& file, const char* password = nullptr);
int qpdfCheck(const QString& file, QString* out = nullptr); // exit code of `qpdf --check`
}
