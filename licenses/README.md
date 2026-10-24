# Third-party licences

PDF Manager is licensed under the **GNU Affero General Public License v3.0**
(see [`../LICENSE`](../LICENSE)). This was chosen because the application links
MuPDF, which Artifex distributes under AGPLv3 (the alternative, an Artifex
commercial licence, was not taken). Anyone who receives the program, or uses it
over a network, is entitled to its complete corresponding source code:
<https://github.com/io-e/pdfmgr>.

| Component | Use in PDF Manager | Licence | File |
|---|---|---|---|
| [MuPDF](https://mupdf.com) (Artifex) | Linked: rendering, text, search | AGPL-3.0-or-later | [MuPDF-AGPL-3.0.txt](MuPDF-AGPL-3.0.txt) |
| [QPDF](https://qpdf.sourceforge.io) | Linked: page operations, writing | Apache-2.0 | [QPDF-Apache-2.0.txt](QPDF-Apache-2.0.txt), [QPDF-NOTICE.md](QPDF-NOTICE.md) |
| [Qt 6](https://www.qt.io) | Linked dynamically (DLLs / shared libraries) | LGPL-3.0-only | [Qt-LGPL-3.0.txt](Qt-LGPL-3.0.txt) |
| [SumatraPDF](https://www.sumatrapdfreader.org) | Shipped as a separate executable (`SumatraPDF.exe`), launched as a process for printing on Windows | GPL-3.0 | [SumatraPDF-GPL-3.0.txt](SumatraPDF-GPL-3.0.txt) |
| [Tesseract OCR](https://github.com/tesseract-ocr/tesseract) | Planned (Phase 3 OCR); not linked or shipped yet | Apache-2.0 | [Tesseract-Apache-2.0.txt](Tesseract-Apache-2.0.txt) |

The MuPDF libraries also bundle third-party code (FreeType, HarfBuzz, libjpeg,
OpenJPEG, jbig2dec, zlib, Gumbo, MuJS, LCMS2...), each under its own permissive
licence; see the MuPDF source tree (`thirdparty/`) for the exact texts. Release
packages must include this folder.

## Qt (LGPLv3)

Qt is used unmodified and linked dynamically, so users can replace the Qt
libraries with their own build. Qt source code: <https://download.qt.io/archive/qt/>
(the Windows build uses Qt 6.8.3; Linux builds use the distribution's Qt package).

## SumatraPDF (GPLv3): source and written offer

The Windows package ships an unmodified, pinned SumatraPDF **3.6.1** portable
executable, downloaded from
`https://www.sumatrapdfreader.org/dl/rel/3.6.1/SumatraPDF-3.6.1-64.zip`
(SHA-256 `98b33a518d42986856d225064b0cd2d3643ecf78cbf84ab873d26cc51877a544`).
It runs as a separate process and is not linked into PDF Manager.

Its complete corresponding source code is available at
<https://github.com/sumatrapdfreader/sumatrapdf/tree/3.6.1rel>.

**Written offer.** For at least three years after we last distribute this
version of SumatraPDF, we will give anyone who asks a copy of its complete
corresponding source code, on a physical medium or by download, for no more than
the cost of providing it. Ask by opening an issue at
<https://github.com/io-e/pdfmgr/issues>.

## QPDF (Apache-2.0)

Redistributions must keep the QPDF copyright and licence notices; see
[QPDF-NOTICE.md](QPDF-NOTICE.md).
