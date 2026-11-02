#include "core/terrain/ChunkBuilder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

using namespace orbis;
using namespace std::chrono_literals;

namespace {
ChunkMesh fakeMesh(const NodeKey& key) {
    ChunkMesh m;
    m.region = key.region();
    return m;
}

std::vector<ChunkBuilder::Result> waitForAll(ChunkBuilder& builder, std::size_t expected) {
    std::vector<ChunkBuilder::Result> all;
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (all.size() < expected && std::chrono::steady_clock::now() < deadline) {
        for (auto& r : builder.takeCompleted(100)) {
            all.push_back(std::move(r));
        }
        std::this_thread::sleep_for(1ms);
    }
    return all;
}
} // namespace

TEST_CASE("builder completes every scheduled request", "[builder]") {
    ChunkBuilder builder(fakeMesh, 3);
    std::vector<NodeRequest> requests;
    for (std::uint32_t i = 0; i < 50; ++i) {
        requests.push_back({NodeKey{CubeFace::PosX, 10, i, 0}, static_cast<double>(i)});
    }
    builder.schedule(requests);
    const auto results = waitForAll(builder, 50);
    CHECK(results.size() == 50);
    CHECK(builder.idle());
}

TEST_CASE("rescheduling drops stale requests", "[builder]") {
    // One slow worker: while it is busy, the queue is replaced, so the old requests never run.
    std::atomic<int> builds{0};
    ChunkBuilder builder(
        [&builds](const NodeKey& key) {
            ++builds;
            std::this_thread::sleep_for(20ms);
            return fakeMesh(key);
        },
        1);

    std::vector<NodeRequest> stale;
    for (std::uint32_t i = 0; i < 20; ++i) {
        stale.push_back({NodeKey{CubeFace::PosY, 8, i, 0}, 1.0});
    }
    builder.schedule(stale);
    std::this_thread::sleep_for(5ms); // let the worker start one
    builder.schedule({{NodeKey{CubeFace::NegZ, 3, 1, 1}, 1.0}});

    const auto results = waitForAll(builder, 2);
    CHECK(results.size() == 2); // the one already running + the new request
    CHECK(builds.load() == 2);
}

TEST_CASE("highest priority is built first", "[builder]") {
    std::mutex orderMutex;
    std::vector<NodeKey> buildOrder;
    std::atomic<bool> release{false};

    ChunkBuilder builder(
        [&](const NodeKey& key) {
            if (key.face == CubeFace::PosX) {
                while (!release) { // the blocker: holds the only worker until we say so
                    std::this_thread::sleep_for(1ms);
                }
            }
            const std::lock_guard lock(orderMutex);
            buildOrder.push_back(key);
            return fakeMesh(key);
        },
        1);

    builder.schedule({{NodeKey{CubeFace::PosX, 1, 0, 0}, 0.0}});
    while (builder.runningCount() == 0) {
        std::this_thread::sleep_for(1ms);
    }
    // The worker is busy, so these three queue up and their order is decided by priority alone.
    builder.schedule({{NodeKey{CubeFace::PosZ, 2, 0, 0}, 1.0},
                      {NodeKey{CubeFace::PosZ, 2, 1, 0}, 100.0},
                      {NodeKey{CubeFace::PosZ, 2, 2, 0}, 10.0}});
    release = true;

    REQUIRE(waitForAll(builder, 4).size() == 4);
    REQUIRE(buildOrder.size() == 4);
    CHECK(buildOrder[1].x == 1); // priority 100
    CHECK(buildOrder[2].x == 2); // priority 10
    CHECK(buildOrder[3].x == 0); // priority 1
}

TEST_CASE("destructor joins promptly with work still queued", "[builder]") {
    const auto start = std::chrono::steady_clock::now();
    {
        ChunkBuilder builder(
            [](const NodeKey& key) {
                std::this_thread::sleep_for(5ms);
                return fakeMesh(key);
            },
            2);
        std::vector<NodeRequest> many;
        for (std::uint32_t i = 0; i < 1000; ++i) {
            many.push_back({NodeKey{CubeFace::NegX, 12, i, 0}, 1.0});
        }
        builder.schedule(many);
        std::this_thread::sleep_for(10ms);
    } // must not wait for 1000 * 5 ms
    CHECK(std::chrono::steady_clock::now() - start < 1s);
}
