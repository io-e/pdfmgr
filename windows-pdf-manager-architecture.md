# Windows PDF Manager — Architecture

Oct 24, 2026 · @ALF

## Objective and constraints

Build a native Windows PDF manager in Qt 6 / C++20. It uses **MuPDF** for rendering, **QPDF** for document operations and **SumatraPDF** for printing, in one installer with no Docker and no Java.

- **Scope:** viewing, printing, page operations (merge, split, rotate, reorder), structural validation, encryption and optimisation.
- **Constraint:** no runtime beyond the application, its DLLs and the bundled `SumatraPDF.exe`.
- **Design rule:** each engine owns one job. No feature is implemented twice.

## Architecture overview

&#91;embedded content: layered architecture · GUI, core, three engines\]

The GUI never touches an engine. SumatraPDF is the only out-of-process component (dashed), which also keeps its GPL licence separate from the application.

## Responsibility matrix

MuPDF draws and reads pages, QPDF rewrites file structure, and SumatraPDF prints.

| Feature | Engine | Integration | Notes |
| --- | --- | --- | --- |
| Render, zoom, navigate | MuPDF | C API, linked | Tiles cached per zoom level |
| Thumbnails | MuPDF | C API | Low-resolution pass on a background thread |
| Text search, selection, extraction | MuPDF | C API | Uses the structured text API (`fz_stext_page`) |
| Annotations, form filling, redaction | MuPDF | C API | QPDF cannot do these |
| Merge, split, extract, reorder pages | QPDF | C++ API, linked | No CLI stage needed |
| Rotate pages | QPDF | C++ API | Sets `/Rotate`, lossless |
| Encrypt, decrypt, permissions | QPDF | C++ API | AES-256 |
| Structural check and repair | QPDF | C++ API | Equivalent of `qpdf --check`; not PDF/A conformance |
| Linearise (fast web view) | QPDF | C++ API |  |
| Size optimisation | QPDF + MuPDF | C++ / C API | QPDF: object streams; MuPDF: garbage collection, deflate, image work |
| Printing | SumatraPDF | `QProcess` (CLI) | Silent, scripted settings; see next section |
| Pass-through and fallback viewing | SumatraPDF | `QProcess` | Optional "Open in Sumatra" action |
| OCR (optional) | Tesseract | C++ API | Adds an invisible text layer, merged back with QPDF |

## SumatraPDF print integration

SumatraPDF is kept as a bundled, version-pinned print engine. A single `PrintService` class invokes it through `QProcess`.

**Why Sumatra:** it already handles fit and shrink scaling, auto-rotation of landscape pages, mixed page sizes, duplex, paper tray, copies and silent printing. Re-implementing that on `QPrinter` costs weeks of edge-case testing.

**Invocation pattern**

```
SumatraPDF.exe -print-to "<printer>" -print-settings "<settings>" -silent "<file.pdf>"
```

| Setting | Values |
| --- | --- |
| Page range | `1-3,5`, `odd`, `even` |
| Scaling | `fit`, `shrink`, `noscale` |
| Orientation | `portrait`, `landscape` |
| Duplex | `simplex`, `duplexlong`, `duplexshort` |
| Colour | `color`, `monochrome` |
| Paper and tray | `paper=A4`, `bin=<n>` |
| Copies | `<n>x`, for example `2x` |

Use `-print-to-default` for the default printer, or `-print-dialog -exit-on-print` when the user wants the native dialog. Verify each flag against the pinned Sumatra version's command-line documentation.

**Implementation rules**

1. Ship a portable `SumatraPDF.exe` in the application folder. Never rely on a system-installed copy.
2. Pass arguments as a `QStringList`, never as one concatenated shell string. This avoids quoting bugs with spaces and Unicode paths.
3. Before printing, save unsaved edits to a temporary PDF (QPDF write). Sumatra prints that file, not the in-memory state.
4. List printers with `QPrinterInfo::availablePrinterNames()` and build the settings string from a `PrintJob` struct in a Qt dialog.
5. Run the print asynchronously, with a timeout (for example 120 s), and report the exit code and stderr to the job panel.
6. Use a print queue: one Sumatra process per job, jobs run in order, and batch printing reuses the same path.
7. Portable Sumatra writes its settings file next to the executable. Under `Program Files` that folder is read-only, which is harmless for printing but expected.

## Threading and concurrency

The UI thread never calls an engine directly. All work goes through a `JobQueue` that returns results by Qt signals.

- **MuPDF:** one base `fz_context` with lock callbacks (`fz_locks_context`) wrapping a set of `std::mutex`. Each render worker uses `fz_clone_context`. The `fz_document` is shared, but each page load and render runs inside the worker's cloned context.
- **QPDF:** a `QPDF` object is not thread-safe. Each write job (merge, split, encrypt) opens its own instance on one worker thread.
- **Sumatra:** an external process, so it is naturally isolated. Use `QProcess` signals and never block.
- **Cancellation:** render jobs carry a generation counter. Scrolling or zooming increments it, and stale tiles are discarded. MuPDF's `fz_cookie` aborts long renders.
- **File safety:** write operations go to a temporary file, then replace the original atomically with `QSaveFile` (atomic on Windows and Linux). The original is never overwritten mid-write.

## Capability gaps and mitigations

The original plan over-assigned QPDF. Four features need a different engine or an explicit limit.

| Gap | Cause | Mitigation |
| --- | --- | --- |
| Real compression | QPDF repacks objects but does not downsample images | Use MuPDF garbage collection and deflate, plus image downsampling through MuPDF; label the feature "Optimise" |
| PDF/A validation | `qpdf --check` is structural only; veraPDF requires Java | Offer structural checks only; state the PDF/A limit in the UI |
| Annotations and forms | Outside QPDF's scope | MuPDF annotation and widget API |
| OCR output | Tesseract writes a separate PDF or hOCR | Generate a text-only layer and overlay it onto the original pages with QPDF |

## Development stack

| Component | Choice | Note |
| --- | --- | --- |
| Language | C++20 |  |
| GUI | Qt 6 Widgets (LTS release) | Installed with the Qt online installer, not vcpkg |
| Build | CMake + Ninja | CMake presets for Debug and Release |
| Compiler | MSVC (Visual Studio 2022 or later) | x64 only |
| Dependencies | vcpkg manifest mode (`vcpkg.json`) | MuPDF, QPDF, Tesseract, Catch2 |
| Rendering | MuPDF | Static link |
| Document operations | QPDF | DLL or static |
| Printing | SumatraPDF portable EXE | Pinned version, hash-checked at build time |
| Tests | Catch2 | Plus a golden-PDF corpus (see roadmap) |
| Deployment | `windeployqt` + WiX (MSI) or Inno Setup (EXE) | Code-sign the binaries and the installer |
| CI | GitHub Actions, Windows runner | Build, test, package; Debian is the primary dev platform (see Cross-platform build) |

## Cross-platform build

Development and integration happen on Debian. Windows binaries come from a Windows CI runner (recommended) or from cross-compilation.

| Option | How | Pros | Cons |
| --- | --- | --- | --- |
| **A. Windows CI runner** (recommended) | Code and test on Debian; GitHub Actions on `windows-latest` builds with MSVC and packages the MSI | Same toolchain as the development stack; reliable `windeployqt` and signing | Needs CI; local Windows debugging needs a VM |
| **B. Cross-compile on Debian** | MinGW-w64 (MXE or llvm-mingw) plus Qt 6 built for MinGW | Everything stays on one Linux machine | Qt, MuPDF and QPDF must be built for MinGW; no MSVC; packaging is more manual |

MSVC does not run natively on Debian. Workarounds exist (`msvc-wine`, clang-cl with `xwin`) but are fragile and not supported here.

**Portability rules**

1. Keep the code portable. CMake, Qt, MuPDF and QPDF all build natively on Debian, so most development and tests run there.
2. Abstract printing behind an `IPrintBackend` interface: `SumatraBackend` on Windows, `CupsBackend` (`lp`) on Linux for development. `PrintService` depends only on the interface.
3. Prefer Qt over Win32 calls. Use `QSaveFile` instead of `ReplaceFileW` for atomic writes on both systems.
4. Isolate Windows-only code behind `#ifdef Q_OS_WIN`.

**Testing**

- **Wine:** the Windows `.exe`, Sumatra included, runs under Wine for quick smoke tests.
- **Real Windows:** still required for final print validation, since printer drivers and the spooler behave differently under Wine.

## Roadmap

The roadmap has three phases. Each phase ends at a measurable gate.

1. **MVP: view, organise, print**
   - Open PDFs in tabs, render, zoom, navigate, thumbnails, text search
   - Rotate, merge, split, extract and reorder pages through the QPDF API
   - Print through `PrintService` (Sumatra) with a settings dialog
   - *Gate:* opens 100% of a 50-file test corpus without crashing; page operations produce QPDF-valid output; prints land correctly on two physical printers
2. **Advanced: protect, optimise, edit**
   - Encryption and permissions, structural check and repair, linearisation
   - Optimise (QPDF + MuPDF), annotations and form filling, optional OCR
   - Batch processing of folders through the job queue
   - *Gate:* the batch of 500 files completes unattended, with a per-file error report
3. **Automation: headless and scriptable**
   - A `pdfmgr-cli` executable sharing the same core library as the GUI
   - JSON job definitions (inputs, operations, print settings) and watched folders
   - Python access through the CLI or a `pybind11` module
   - *Gate:* every GUI operation is reproducible from a JSON job

Plan for a core-library split from day one: `pdfcore` (engines, jobs, no Qt Widgets dependency) plus `pdfgui`. Phase 3 then needs no refactor.

## Licensing and distribution

MuPDF decides the licence of the whole application: linking it makes the application AGPLv3, unless you buy a commercial licence from Artifex.

| Component | Licence | Obligation when distributing |
| --- | --- | --- |
| MuPDF | AGPLv3 or commercial | Publish the full application source under AGPLv3, or buy a commercial licence |
| SumatraPDF | GPLv3 | Bundled as a separate program (aggregation): ship its licence and its source, or a written offer for it |
| Qt 6 | LGPLv3 / GPL / commercial | Link dynamically, allow relinking, ship the licence; avoid GPL-only modules |
| QPDF | Apache 2.0 | Ship the licence and NOTICE |
| Tesseract | Apache 2.0 | Ship the licence and NOTICE |

- **Open-source route:** AGPLv3 for the application. AGPLv3 and GPLv3 are compatible, so bundling Sumatra is straightforward.
- **Closed-source route:** an Artifex commercial licence for MuPDF, or replace MuPDF with Qt PDF (PDFium) and keep everything else. Sumatra stays a separate GPL program either way.
- Add an "About / Licences" dialog and a `licenses/` folder in the installer.

This section is not legal advice. Have the distribution model checked before release.

## Risks and decisions

| Item | Type | Detail |
| --- | --- | --- |
| Keep SumatraPDF for printing | Decision | Its proven print pipeline outweighs shipping a GPL EXE |
| QPDF via C++ API from the start | Decision | Drops the CLI stage from the original plan |
| MuPDF licence model | Open question | &#32;or Artifex commercial licence? Decide before the first public release |
| Sumatra CLI changes between versions | Risk | Pin the version and run a print-argument smoke test in CI |
| Driver-specific print quirks | Risk | Test on at least two printer brands; expose a "use printer dialog" fallback |
| Malformed or hostile PDFs | Risk | Render in workers with `fz_try`; run a fuzzing corpus; consider out-of-process rendering later |
| Edits lost on crash | Risk | Atomic temp-file writes and an autosave of the operation history |
