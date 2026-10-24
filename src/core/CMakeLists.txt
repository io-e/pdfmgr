add_library(pdfcore STATIC
  MuPdfContext.h MuPdfContext.cpp
  Document.h Document.cpp
  PageOps.h PageOps.cpp
  AtomicWrite.h AtomicWrite.cpp
  CancelToken.h CancelToken.cpp
  JobQueue.h JobQueue.cpp
  TileCache.h TileCache.cpp
  PrintJob.h
  IPrintBackend.h
  CupsBackend.h CupsBackend.cpp
  SumatraBackend.h SumatraBackend.cpp
  PrintService.h PrintService.cpp
  Version.h Version.cpp
  Logging.h Logging.cpp
)
target_include_directories(pdfcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# QtGui only for QImage; no Qt Widgets dependency in the core.
target_link_libraries(pdfcore PUBLIC Qt6::Core Qt6::Gui PRIVATE pdfmgr_mupdf pdfmgr_qpdf)
if(WIN32)
  target_link_libraries(pdfcore PRIVATE dbghelp)
endif()
