#pragma once
#include <QDialog>
#include "PrintJob.h"
class QComboBox; class QLineEdit; class QSpinBox;

// Builds a PrintJob; printers come from QPrinterInfo.
class PrintDialog : public QDialog {
    Q_OBJECT
public:
    explicit PrintDialog(int pageCount, QWidget* parent = nullptr);
    pdfcore::PrintJob job() const;
private:
    QComboBox *m_printer, *m_scaling, *m_orientation, *m_duplex, *m_color, *m_paper;
    QLineEdit* m_range;
    QSpinBox *m_copies, *m_bin;
};
