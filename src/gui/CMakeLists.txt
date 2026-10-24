add_executable(pdfgui WIN32
  main.cpp MainWindow.h MainWindow.cpp DocumentTab.h DocumentTab.cpp ThumbList.h ThumbList.cpp PrintDialog.h PrintDialog.cpp)
target_link_libraries(pdfgui PRIVATE pdfcore Qt6::Widgets Qt6::PrintSupport)
install(TARGETS pdfgui RUNTIME DESTINATION bin)

# Build-time version info for the About dialog.
execute_process(COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE PDFMGR_GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT PDFMGR_GIT_HASH)
  set(PDFMGR_GIT_HASH "unknown")
endif()
string(TIMESTAMP PDFMGR_BUILD_DATE "%Y-%m-%d")
target_compile_definitions(pdfgui PRIVATE
  PDFMGR_VERSION="${PROJECT_VERSION}"
  PDFMGR_GIT_HASH="${PDFMGR_GIT_HASH}"
  PDFMGR_BUILD_DATE="${PDFMGR_BUILD_DATE}")
