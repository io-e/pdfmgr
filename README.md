# PDF Manager — Phase 1 (MVP)

Qt 6 / C++20. `pdfcore` (MuPDF render/search, QPDF page ops, JobQueue, PrintService) + `pdfgui` (Qt Widgets).
## Documentation
- [Architecture (document de référence)](docs/windows-pdf-manager-architecture.md)
- [Dossier d'architecture et de planification](docs/dossier-architecture-planification.md)


## Debian
    sudo apt install qt6-base-dev cmake ninja-build g++ libmupdf-dev libqpdf-dev catch2 qpdf cups-client pkg-config
    cmake --preset debug && cmake --build --preset debug && ctest --preset debug
    ./build/debug/src/gui/pdfgui file.pdf

## Windows
`.github/workflows/windows.yml` + `vcpkg.json` — **untested skeleton**.

## Licence
Links MuPDF → AGPLv3 unless an Artifex commercial licence is obtained.
