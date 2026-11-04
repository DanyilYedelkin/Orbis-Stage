#include "app/gl/GpuTimers.hpp"

namespace orbis::gl {

std::string_view gpuPassName(GpuPass pass) {
    constexpr std::array<std::string_view, GpuTimers::kPasses> names = {
        "shadows", "sky", "terrain", "clouds", "tone map", "ui"};
    return names[static_cast<std::size_t>(pass)];
}

GpuTimers::GpuTimers() {
    for (Frame& f : frames_) {
        glCreateQueries(GL_TIMESTAMP, static_cast<GLsizei>(f.queries.size()), f.queries.data());
    }
}

GpuTimers::~GpuTimers() {
    for (Frame& f : frames_) {
        glDeleteQueries(static_cast<GLsizei>(f.queries.size()), f.queries.data());
    }
}

bool GpuTimers::collect(Frame& frame, bool wait, std::array<double, kPasses>& out) {
    out.fill(0.0);
    for (std::size_t p = 0; p < kPasses; ++p) {
        if (!frame.used[p]) {
            continue;
        }
        const GLuint end = frame.queries[2 * p + 1];
        if (!wait) {
            GLint available = 0;
            glGetQueryObjectiv(end, GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == 0) {
                return false;
            }
        }
        GLuint64 t0 = 0;
        GLuint64 t1 = 0;
        glGetQueryObjectui64v(frame.queries[2 * p], GL_QUERY_RESULT, &t0);
        glGetQueryObjectui64v(end, GL_QUERY_RESULT, &t1);
        out[p] = static_cast<double>(t1 - t0) * 1e-6;
    }
    return true;
}

void GpuTimers::beginFrame() {
    current_ = (current_ + 1) % kFrames;
    Frame& oldest = frames_[static_cast<std::size_t>(current_)];
    std::array<double, kPasses> ms{};
    if (started_ && collect(oldest, false, ms)) {
        for (std::size_t p = 0; p < kPasses; ++p) {
            averaged_[p] += 0.1 * (ms[p] - averaged_[p]);
        }
    }
    oldest.used.fill(false);
    started_ = true;
}

void GpuTimers::begin(GpuPass pass) {
    const auto p = static_cast<std::size_t>(pass);
    Frame& f = frames_[static_cast<std::size_t>(current_)];
    glQueryCounter(f.queries[2 * p], GL_TIMESTAMP);
    f.used[p] = true;
}

void GpuTimers::end(GpuPass pass) {
    const auto p = static_cast<std::size_t>(pass);
    glQueryCounter(frames_[static_cast<std::size_t>(current_)].queries[2 * p + 1], GL_TIMESTAMP);
}

void GpuTimers::finishFrame() {
    std::array<double, kPasses> ms{};
    Frame& f = frames_[static_cast<std::size_t>(current_)];
    collect(f, true, ms);
    for (std::size_t p = 0; p < kPasses; ++p) {
        if (f.used[p]) {
            averaged_[p] = ms[p];
        }
    }
}

double GpuTimers::totalMilliseconds() const {
    double total = 0.0;
    for (const double ms : averaged_) {
        total += ms;
    }
    return total;
}

} // namespace orbis::gl
