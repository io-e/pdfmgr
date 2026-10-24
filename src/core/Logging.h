#pragma once
#include <QString>

namespace pdfcore {
// Call once at startup. Messages go to `logDir()/pdfmgr.log`, rotated at ~2 MB.
void initLogging();
// Returns the directory where log files are stored.
QString logDir();
}
