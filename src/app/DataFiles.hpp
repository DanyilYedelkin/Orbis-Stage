#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace orbis {

/// Not the working directory, which depends on how the program was started.
std::filesystem::path executableDirectory();

/// <executable dir>/data first, then <source tree>/data.
std::optional<std::filesystem::path> findDataFile(std::string_view name);

std::optional<std::filesystem::path> findDataDirectory(std::string_view name);

} // namespace orbis
