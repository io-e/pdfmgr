#include <catch2/catch_session.hpp>
#include <QGuiApplication>
#include <cstring>
#include "Fixtures.h"
int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--write-fixture") == 0) { fixtures::writePdf(argv[2], 3, "Smoke"); return 0; }
    QGuiApplication app(argc, argv); // event loop for PrintService/JobQueue; QImage needs no display
    return Catch::Session().run(argc, argv);
}
