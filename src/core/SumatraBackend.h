#pragma once
#include "IPrintBackend.h"
namespace pdfcore {
// Windows production backend: bundled portable SumatraPDF.exe.
// The argument/settings builders are portable (unit-tested on Linux);
// PrintService only picks this backend by default under Q_OS_WIN.
class SumatraBackend : public IPrintBackend {
public:
    explicit SumatraBackend(QString exePath = defaultExePath()) : m_exe(std::move(exePath)) {}
    static QString defaultExePath();               // <appdir>/SumatraPDF.exe
    static QString settingsString(const PrintJob& job);
    QString name() const override { return QStringLiteral("SumatraPDF"); }
    QString program() const override { return m_exe; }
    QStringList arguments(const PrintJob& job) const override;
private:
    QString m_exe;
};
}
