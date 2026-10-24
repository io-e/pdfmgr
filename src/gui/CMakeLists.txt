add_executable(pdfgui WIN32
  main.cpp MainWindow.h MainWindow.cpp DocumentTab.h DocumentTab.cpp PrintDialog.h PrintDialog.cpp)
target_link_libraries(pdfgui PRIVATE pdfcore Qt6::Widgets Qt6::PrintSupport)
install(TARGETS pdfgui RUNTIME DESTINATION bin)
