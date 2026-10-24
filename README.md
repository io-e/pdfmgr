# PDF Manager — Phase 1 (MVP)

Qt 6 / C++20. `pdfcore` (MuPDF rendering/search, QPDF page operations, JobQueue, PrintService) + `pdfgui` (Qt Widgets).

## Documentation
- [Architecture (reference document)](docs/windows-pdf-manager-architecture.md)
- [Architecture and development planning dossier](docs/architecture-and-planning.md)

## Debian / Ubuntu
Requires Qt 6.4+, qpdf 11.9+ and MuPDF. Tested on Debian 13 (Qt 6.8, qpdf 12.2, shared MuPDF 1.25);
Ubuntu 24.04 (Qt 6.4, qpdf 11.9, static MuPDF 1.23) is built by the `ubuntu` CI job.

    sudo apt install qt6-base-dev cmake ninja-build g++ pkg-config \
        libmupdf-dev libqpdf-dev catch2 \
        libharfbuzz-dev libfreetype-dev libjbig2dec0-dev libopenjp2-7-dev \
        libgumbo-dev libmujs-dev libjpeg-dev zlib1g-dev cups-client
    cmake --preset debug && cmake --build --preset debug && ctest --preset debug
    ./build/debug/src/gui/pdfgui file.pdf

The MuPDF dependency libraries (`harfbuzz` … `mujs`) are only needed when the distribution ships a
static `libmupdf.a` (Ubuntu); CMake then links them via pkg-config. Neither building nor testing needs a
display (tests run with `QT_QPA_PLATFORM=offscreen`), and the tests use the QPDF library, not the `qpdf` CLI.

### Print-to-PDF integration test
`print_to_pdf` (ctest label `print`) prints a 3-page landscape PDF through the real backend and checks
the output's page count, orientation and text. It is reported as *Skipped* unless a PDF printer is set up:

    sudo apt install cups printer-driver-cups-pdf
    scripts/setup-cups-pdf.sh          # queue "PDF", output in ~/PDF
    PDFMGR_TEST_PRINTER=PDF PDFMGR_TEST_PRINT_OUT=$HOME/PDF ctest --preset debug -L print

On Windows CI the same test runs through SumatraPDF into "Microsoft Print to PDF" attached to a local
port that is a fixed file path (`PDFMGR_TEST_PRINT_OUT` = that file, `PDFMGR_SUMATRA` = the exe).

Compiler warnings depend on the qpdf version; the build defines `POINTERHOLDER_TRANSITION=4` for qpdf 11.

## Windows
GitHub Actions (`.github/workflows/windows.yml`, job `windows`): MSVC x64, Qt 6.8 via
`install-qt-action`, MuPDF (`libmupdf` port, static) and QPDF from vcpkg pinned by `builtin-baseline`
in `vcpkg.json`, then `windeployqt` with a pinned, hash-checked SumatraPDF. The artifact is an unpacked
folder; the MSI installer and code signing are not done yet. Printing and file locking have not been
validated on a real Windows machine.

## Licence
GNU AGPLv3, see [`LICENSE`](LICENSE). PDF Manager links MuPDF (AGPLv3). Third-party licences, including
the SumatraPDF (GPLv3) source offer, are in [`licenses/`](licenses/README.md).
