find_package(Catch2 3 REQUIRED)
find_program(QPDF_EXE qpdf REQUIRED)
add_executable(pdfcore_tests
  main.cpp Fixtures.h Fixtures.cpp
  test_document.cpp test_pageops.cpp test_print.cpp test_jobqueue.cpp)
target_link_libraries(pdfcore_tests PRIVATE pdfcore Qt6::Test Catch2::Catch2)
target_compile_definitions(pdfcore_tests PRIVATE QPDF_EXE="${QPDF_EXE}")
include(Catch)
catch_discover_tests(pdfcore_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME gui_smoke COMMAND pdfgui --smoke-test ${CMAKE_CURRENT_BINARY_DIR}/smoke.pdf)
set_tests_properties(gui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" PASS_REGULAR_EXPRESSION "SMOKE OK tabs=1 pages=3"
  FIXTURES_REQUIRED smokepdf)
add_test(NAME gui_smoke_fixture COMMAND pdfcore_tests --write-fixture ${CMAKE_CURRENT_BINARY_DIR}/smoke.pdf)
set_tests_properties(gui_smoke_fixture PROPERTIES FIXTURES_SETUP smokepdf)
