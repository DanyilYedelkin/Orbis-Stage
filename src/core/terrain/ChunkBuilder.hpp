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

/// Worker pool for chunk meshes. schedule() replaces the queue every frame, so stale requests
/// are dropped and the rest re-prioritised; running or uncollected nodes are not rebuilt.
/// `build` runs concurrently.
class ChunkBuilder {
public:
    using BuildFunction = std::function<ChunkMesh(const NodeKey&)>;
    using Result = std::pair<NodeKey, ChunkMesh>;

    ChunkBuilder(BuildFunction build, unsigned threadCount);
    ~ChunkBuilder() = default;

    ChunkBuilder(const ChunkBuilder&) = delete;
    ChunkBuilder& operator=(const ChunkBuilder&) = delete;

    /// Higher priority first.
    void schedule(std::vector<NodeRequest> requests);

    std::vector<Result> takeCompleted(std::size_t maxCount);

    bool idle() const;

    std::size_t pendingCount() const;
    std::size_t runningCount() const;

    static unsigned defaultThreadCount();

private:
    void workerLoop(std::stop_token stop);

    BuildFunction build_;

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::vector<NodeRequest> pending_; ///< Most urgent last.
    std::unordered_set<NodeKey, NodeKeyHash> running_;
    std::vector<Result> completed_;

    // Last: joins before the members the workers use are destroyed.
    std::vector<std::jthread> workers_;
};

} // namespace orbis
