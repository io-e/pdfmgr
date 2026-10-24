#include <catch2/catch_test_macros.hpp>
#include "JobQueue.h"
#include <QEventLoop>
#include <QTimer>
#include <QSignalSpy>
#include <thread>
using namespace pdfcore;

TEST_CASE("JobQueue delivers results and errors via signals", "[jobs]") {
    JobQueue q;
    QSignalSpy ok(&q, &JobQueue::finished), bad(&q, &JobQueue::failed);
    auto id = q.submit([] { return QVariant(42); });
    q.submit([]() -> QVariant { throw std::runtime_error("nope"); });
    QEventLoop l; QTimer::singleShot(3000, &l, &QEventLoop::quit);
    QTimer poll; QObject::connect(&poll, &QTimer::timeout, &l, [&] { if (ok.size() && bad.size()) l.quit(); }); poll.start(5);
    l.exec();
    REQUIRE(ok.size() == 1); CHECK(ok[0][0].toULongLong() == id); CHECK(ok[0][1].toInt() == 42);
    REQUIRE(bad.size() == 1); CHECK(bad[0][1].toString() == "nope");
}
TEST_CASE("stale render generations are discarded", "[jobs]") {
    JobQueue q(nullptr, 2);
    QSignalSpy done(&q, &JobQueue::renderFinished), dropped(&q, &JobQueue::renderDiscarded);
    quint64 g1 = q.bumpGeneration();
    q.submitRender(g1, [](const std::atomic<bool>& abort) {
        for (int i = 0; i < 200 && !abort; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return QVariant(abort.load() ? "aborted" : "finished");
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    quint64 g2 = q.bumpGeneration(); // user zoomed: g1 is stale
    q.submitRender(g2, [](const std::atomic<bool>&) { return QVariant("fresh"); });
    QEventLoop l; QTimer::singleShot(3000, &l, &QEventLoop::quit);
    QTimer poll; QObject::connect(&poll, &QTimer::timeout, &l, [&] { if (done.size() + dropped.size() >= 2) l.quit(); }); poll.start(5);
    l.exec();
    REQUIRE(done.size() == 1); CHECK(done[0][1].toULongLong() == g2); CHECK(done[0][2].toString() == "fresh");
    REQUIRE(dropped.size() == 1); CHECK(dropped[0][1].toULongLong() == g1);
}
