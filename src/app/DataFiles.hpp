#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace orbis {

/// Directory containing the running executable (not the working directory, which depends on
/// how the program was started: double-click, Visual Studio, terminal, CI).
std::filesystem::path executableDirectory();

/// Looks for a data file, in this order:
///   1. <executable dir>/data/<name>   - copied there by the build, so a built exe is portable
///   2. <source tree>/data/<name>      - so running straight from the build tree always works
std::optional<std::filesystem::path> findDataFile(std::string_view name);

} // namespace orbis
