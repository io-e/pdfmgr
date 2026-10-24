#pragma once
#include "IPrintBackend.h"
namespace pdfcore {
// Linux development backend: `lp`.
class CupsBackend : public IPrintBackend {
public:
    explicit CupsBackend(QString lpProgram = QStringLiteral("lp")) : m_program(std::move(lpProgram)) {}
    QString name() const override { return QStringLiteral("CUPS"); }
    QString program() const override { return m_program; }
    QStringList arguments(const PrintJob& job) const override;
private:
    QString m_program;
};
}
