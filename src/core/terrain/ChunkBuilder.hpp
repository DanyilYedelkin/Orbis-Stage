#pragma once

#include "core/terrain/ChunkMesh.hpp"
#include "core/terrain/Quadtree.hpp"

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace orbis {

/// Builds chunk meshes on worker threads.
///
/// The queue is REPLACED every frame (schedule()), not appended to. The camera moves, so last
/// frame's wishes are stale: requests nobody wants any more are dropped before a thread wastes
/// time on them, and the rest is re-prioritised. Requests that are already running or finished
/// but not yet collected are never started twice.
///
/// Threading model: one mutex protects all shared state; work happens outside the lock. Build
/// functions must be thread-safe - ours only read immutable data (ellipsoid, height field).
class ChunkBuilder {
public:
    using BuildFunction = std::function<ChunkMesh(const NodeKey&)>;
    using Result = std::pair<NodeKey, ChunkMesh>;

    ChunkBuilder(BuildFunction build, unsigned threadCount);
    ~ChunkBuilder() = default; // std::jthread: requests stop and joins automatically

    ChunkBuilder(const ChunkBuilder&) = delete;
    ChunkBuilder& operator=(const ChunkBuilder&) = delete;

    /// Replaces the pending queue. Higher priority is built first.
    void schedule(std::vector<NodeRequest> requests);

    /// Finished meshes, at most maxCount (lets the caller bound GPU uploads per frame).
    std::vector<Result> takeCompleted(std::size_t maxCount);

    /// Nothing pending, nothing running, nothing waiting to be collected.
    bool idle() const;

    std::size_t pendingCount() const;
    std::size_t runningCount() const;

    static unsigned defaultThreadCount();

private:
    void workerLoop(std::stop_token stop);

    BuildFunction build_;

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::vector<NodeRequest> pending_; ///< Sorted ascending: back() is the most urgent.
    std::unordered_set<NodeKey, NodeKeyHash> running_;
    std::vector<Result> completed_;

    // Declared LAST, so it is destroyed FIRST: the threads stop and join while the mutex,
    // queue and build function they use are still alive.
    std::vector<std::jthread> workers_;
};

} // namespace orbis
