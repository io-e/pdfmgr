#include <catch2/catch_session.hpp>
#include <QGuiApplication>
#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>
#include <QString>
#include "Fixtures.h"
int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--write-fixture") == 0) { fixtures::writePdf(argv[2], 3, "Smoke"); return 0; }
    if (argc == 4 && std::strcmp(argv[1], "--fake-backend") == 0) { // print backend stand-in for test_print
        const char* mode = argv[2];
        if (!std::strcmp(mode, "check")) {
            QString out;
            int rc = fixtures::qpdfCheck(QString::fromLocal8Bit(argv[3]), &out);
            if (rc == 0) std::fprintf(stderr, "checked\n"); else std::fprintf(stderr, "%s\n", qPrintable(out));
            return rc;
        }
        if (!std::strcmp(mode, "fail")) { std::fprintf(stderr, "boom\n"); return 3; }
        if (!std::strcmp(mode, "sleep")) { std::this_thread::sleep_for(std::chrono::seconds(30)); return 0; }
        return 0;
    }
    // Tests never need a display; also covers ctest-time test discovery.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv); // event loop for PrintService/JobQueue; QImage needs no display
    return Catch::Session().run(argc, argv);
}
