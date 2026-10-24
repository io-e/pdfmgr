#include <catch2/catch_test_macros.hpp>
#include "JobQueue.h"
#include <QEventLoop>
#include <QTimer>
#include <QSignalSpy>
#include <thread>
#include <QElapsedTimer>
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
    q.submitRender(g1, [](const CancelToken& c) {
        for (int i = 0; i < 200 && !c.isCancelled(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return QVariant(c.isCancelled() ? "aborted" : "finished");
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    quint64 g2 = q.bumpGeneration(); // user zoomed: g1 is stale
    q.submitRender(g2, [](const CancelToken&) { return QVariant("fresh"); });
    QEventLoop l; QTimer::singleShot(3000, &l, &QEventLoop::quit);
    QTimer poll; QObject::connect(&poll, &QTimer::timeout, &l, [&] { if (done.size() + dropped.size() >= 2) l.quit(); }); poll.start(5);
    l.exec();
    REQUIRE(done.size() == 1); CHECK(done[0][1].toULongLong() == g2); CHECK(done[0][2].toString() == "fresh");
    REQUIRE(dropped.size() == 1); CHECK(dropped[0][1].toULongLong() == g1);
}

TEST_CASE("bumpGeneration cancels the running render's fz_cookie immediately", "[jobs]") {
    JobQueue q(nullptr, 1);
    std::atomic<bool> started{false}, sawCancel{false};
    quint64 g = q.bumpGeneration();
    q.submitRender(g, [&](const CancelToken& c) {
        started = true;
        for (int i = 0; i < 400 && !c.isCancelled(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        sawCancel = c.isCancelled();
        return QVariant();
    });
    while (!started) std::this_thread::yield();
    QElapsedTimer t; t.start();
    q.bumpGeneration();
    q.waitForDone();
    CHECK(sawCancel.load());
    CHECK(t.elapsed() < 1000); // not the full 2 s loop
}

TEST_CASE("waitForOwner waits only for that owner's jobs", "[jobs]") {
    JobQueue q(nullptr, 2);
    const quint64 a = JobQueue::newOwnerId(), b = JobQueue::newOwnerId();
    CHECK(b > a);
    std::atomic<bool> release{false}, aDone{false};
    q.submit([&] { while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(2)); return QVariant(); }, b);
    q.submit([&] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); aDone = true; return QVariant(); }, a);
    q.waitForOwner(a);          // must return although owner b is still blocked
    CHECK(aDone.load());
    CHECK(q.pending(b) == 1);
    release = true;
    q.waitForOwner(b);
    CHECK(q.pending(b) == 0);
}

TEST_CASE("cancelOwner skips that owner's queued jobs", "[jobs]") {
    JobQueue q(nullptr, 1);
    const quint64 a = JobQueue::newOwnerId(), b = JobQueue::newOwnerId();
    std::atomic<bool> release{false}; std::atomic<int> ranA{0}, ranB{0};
    q.submit([&] { while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(2)); return QVariant(); }, b);
    for (int i = 0; i < 5; ++i) q.submit([&] { ++ranA; return QVariant(); }, a);
    q.submit([&] { ++ranB; return QVariant(); }, b);
    q.cancelOwner(a);
    release = true;
    q.waitForDone();
    CHECK(ranA == 0);
    CHECK(ranB == 1);
}
