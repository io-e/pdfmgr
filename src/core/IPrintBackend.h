#pragma once
#include "PrintJob.h"
#include <QStringList>
namespace pdfcore {
class IPrintBackend {
public:
    virtual ~IPrintBackend() = default;
    virtual QString name() const = 0;
    virtual QString program() const = 0;
    virtual QStringList arguments(const PrintJob& job) const = 0; // never a shell string
};
}
