#include "core/terrain/ChunkBuilder.hpp"

#include <algorithm>
#include <exception>
#include <iostream>

namespace orbis {

ChunkBuilder::ChunkBuilder(BuildFunction build, unsigned threadCount) : build_(std::move(build)) {
    threadCount = std::max(threadCount, 1u);
    workers_.reserve(threadCount);
    for (unsigned i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this](std::stop_token stop) { workerLoop(stop); });
    }
}

unsigned ChunkBuilder::defaultThreadCount() {
    // Leave one core for the render thread.
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 1 ? hw - 1 : 1;
}

void ChunkBuilder::schedule(std::vector<NodeRequest> requests) {
    {
        const std::lock_guard lock(mutex_);
        std::erase_if(requests, [this](const NodeRequest& r) {
            if (running_.contains(r.key)) {
                return true;
            }
            return std::any_of(completed_.begin(), completed_.end(),
                               [&](const Result& done) { return done.first == r.key; });
        });
        std::sort(requests.begin(), requests.end(),
                  [](const NodeRequest& a, const NodeRequest& b) { return a.priority < b.priority; });
        pending_ = std::move(requests);
    }
    wake_.notify_all();
}

std::vector<ChunkBuilder::Result> ChunkBuilder::takeCompleted(std::size_t maxCount) {
    const std::lock_guard lock(mutex_);
    const std::size_t count = std::min(maxCount, completed_.size());
    std::vector<Result> out;
    out.reserve(count);
    std::move(completed_.end() - static_cast<std::ptrdiff_t>(count), completed_.end(),
              std::back_inserter(out));
    completed_.resize(completed_.size() - count);
    return out;
}

bool ChunkBuilder::idle() const {
    const std::lock_guard lock(mutex_);
    return pending_.empty() && running_.empty() && completed_.empty();
}

std::size_t ChunkBuilder::pendingCount() const {
    const std::lock_guard lock(mutex_);
    return pending_.size();
}

std::size_t ChunkBuilder::runningCount() const {
    const std::lock_guard lock(mutex_);
    return running_.size();
}

void ChunkBuilder::workerLoop(std::stop_token stop) {
    while (true) {
        NodeKey key;
        {
            std::unique_lock lock(mutex_);
            // Wakes on new work OR on stop request (jthread destructor) - no lost wake-ups,
            // because the predicate is re-checked under the lock.
            const bool hasWork = wake_.wait(lock, stop, [this] { return !pending_.empty(); });
            // wait() returns as soon as the predicate holds WITHOUT looking at the stop token,
            // so check it explicitly - otherwise shutdown would first drain the whole queue.
            if (!hasWork || stop.stop_requested()) {
                return;
            }
            key = pending_.back().key;
            pending_.pop_back();
            running_.insert(key);
        }

        // The expensive part runs WITHOUT the lock, so other workers and the main thread
        // are never blocked by mesh generation.
        ChunkMesh mesh;
        bool ok = true;
        try {
            mesh = build_(key);
        } catch (const std::exception& e) {
            ok = false;
            std::cerr << "[ChunkBuilder] build failed: " << e.what() << '\n';
        }

        {
            const std::lock_guard lock(mutex_);
            running_.erase(key);
            if (ok) {
                completed_.emplace_back(key, std::move(mesh));
            }
        }
    }
}

} // namespace orbis
