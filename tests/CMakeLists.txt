find_package(Catch2 3 REQUIRED)
add_executable(pdfcore_tests
  main.cpp Fixtures.h Fixtures.cpp
  test_document.cpp test_pageops.cpp test_print.cpp test_jobqueue.cpp test_fileops.cpp)
# The qpdf C++ API (QPDFJob) replaces the qpdf CLI for fixtures and --check,
# so the tests need no qpdf executable (not available on the Windows runner).
target_link_libraries(pdfcore_tests PRIVATE pdfcore pdfmgr_qpdf Qt6::Test Catch2::Catch2)
include(Catch)
# PRE_TEST: list the tests when ctest runs (not at build time), so building
# never needs a display; offscreen for both listing and running.
catch_discover_tests(pdfcore_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME gui_smoke COMMAND pdfgui --smoke-test ${CMAKE_CURRENT_BINARY_DIR}/smoke.pdf)
set_tests_properties(gui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" PASS_REGULAR_EXPRESSION "SMOKE OK tabs=1 pages=3 apply=ok"
  FIXTURES_REQUIRED smokepdf)
add_test(NAME gui_smoke_fixture COMMAND pdfcore_tests --write-fixture ${CMAKE_CURRENT_BINARY_DIR}/smoke.pdf)
set_tests_properties(gui_smoke_fixture PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" FIXTURES_SETUP smokepdf)
