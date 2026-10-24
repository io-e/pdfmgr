#include "PrintDialog.h"
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPrinterInfo>
using namespace pdfcore;

PrintDialog::PrintDialog(int pageCount, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Print"));
    auto* f = new QFormLayout(this);
    m_printer = new QComboBox;
    m_printer->addItem(tr("(Default printer)"), QString());
    for (const auto& n : QPrinterInfo::availablePrinterNames()) m_printer->addItem(n, n);
    m_range = new QLineEdit; m_range->setPlaceholderText(tr("All pages (1-%1). e.g. 1-3,5 / odd / even").arg(pageCount));
    m_scaling = new QComboBox; m_scaling->addItems({tr("Shrink"), tr("Fit"), tr("No scaling")});
    m_orientation = new QComboBox; m_orientation->addItems({tr("Auto"), tr("Portrait"), tr("Landscape")});
    m_duplex = new QComboBox; m_duplex->addItems({tr("Printer default"), tr("Simplex"), tr("Duplex long edge"), tr("Duplex short edge")});
    m_color = new QComboBox; m_color->addItems({tr("Printer default"), tr("Color"), tr("Monochrome")});
    m_paper = new QComboBox; m_paper->addItems({QString(), "A4", "A3", "A5", "Letter", "Legal"});
    m_bin = new QSpinBox; m_bin->setRange(-1, 99); m_bin->setSpecialValueText(tr("Default"));
    m_copies = new QSpinBox; m_copies->setRange(1, 999);
    f->addRow(tr("Printer"), m_printer); f->addRow(tr("Pages"), m_range);
    f->addRow(tr("Scaling"), m_scaling); f->addRow(tr("Orientation"), m_orientation);
    f->addRow(tr("Duplex"), m_duplex); f->addRow(tr("Colour"), m_color);
    f->addRow(tr("Paper"), m_paper); f->addRow(tr("Tray"), m_bin); f->addRow(tr("Copies"), m_copies);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    f->addRow(bb);
}

PrintJob PrintDialog::job() const {
    PrintJob j;
    j.printer = m_printer->currentData().toString();
    j.pageRange = m_range->text().trimmed();
    j.scaling = std::array{PrintJob::Scaling::Shrink, PrintJob::Scaling::Fit, PrintJob::Scaling::NoScale}[m_scaling->currentIndex()];
    j.orientation = PrintJob::Orientation(m_orientation->currentIndex());
    j.duplex = PrintJob::Duplex(m_duplex->currentIndex());
    j.color = PrintJob::Color(m_color->currentIndex());
    j.paper = m_paper->currentText();
    j.bin = m_bin->value();
    j.copies = m_copies->value();
    return j;
}
