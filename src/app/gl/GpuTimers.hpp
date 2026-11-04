#pragma once

#include "app/gl/GlHandle.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace orbis::gl {

enum class GpuPass { Shadows, Sky, Terrain, Clouds, ToneMap, Ui, Count };

std::string_view gpuPassName(GpuPass pass);

/// Per-pass GPU time from GL_TIMESTAMP queries. Results are read kFrames later, when they are
/// ready, so measuring never stalls the pipeline; the panel shows an exponential average.
class GpuTimers {
public:
    static constexpr int kFrames = 4;
    static constexpr auto kPasses = static_cast<std::size_t>(GpuPass::Count);

    GpuTimers();
    ~GpuTimers();
    GpuTimers(const GpuTimers&) = delete;
    GpuTimers& operator=(const GpuTimers&) = delete;

    /// Collects the oldest frame's results and starts a new frame.
    void beginFrame();
    void begin(GpuPass pass);
    void end(GpuPass pass);

    /// Blocks until the current frame's queries are done and takes them as the new values,
    /// without averaging (screenshots, benchmarks).
    void finishFrame();

    double milliseconds(GpuPass pass) const { return averaged_[static_cast<std::size_t>(pass)]; }
    double totalMilliseconds() const;

    class Scope {
    public:
        Scope(GpuTimers& timers, GpuPass pass) : timers_(timers), pass_(pass) { timers_.begin(pass_); }
        ~Scope() { timers_.end(pass_); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        GpuTimers& timers_;
        GpuPass pass_;
    };
    Scope scope(GpuPass pass) { return Scope(*this, pass); }

private:
    struct Frame {
        std::array<GLuint, kPasses * 2> queries{};
        std::array<bool, kPasses> used{};
    };
    bool collect(Frame& frame, bool wait, std::array<double, kPasses>& out);

    std::array<Frame, kFrames> frames_{};
    std::array<double, kPasses> averaged_{};
    int current_ = 0;
    bool started_ = false;
};

} // namespace orbis::gl
