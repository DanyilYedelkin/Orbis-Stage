#pragma once

namespace orbis::gl {

/// Routes driver errors / warnings to stderr via GL_KHR_debug (core in 4.3).
/// Only active in a debug context (debug builds): synchronous output costs performance.
void enableDebugOutput();

} // namespace orbis::gl
