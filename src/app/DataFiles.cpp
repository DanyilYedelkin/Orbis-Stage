#include "app/DataFiles.hpp"

#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace orbis {

std::filesystem::path executableDirectory() {
#ifdef _WIN32
    // GetModuleFileNameW truncates silently; grow the buffer until the path fits.
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::filesystem::current_path();
        }
        if (length < buffer.size()) {
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::current_path() : exe.parent_path();
#endif
}

std::optional<std::filesystem::path> findDataFile(std::string_view name) {
    const std::filesystem::path candidates[] = {
        executableDirectory() / "data" / name,
        std::filesystem::path(ORBIS_SOURCE_DATA_DIR) / name,
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> findDataDirectory(std::string_view name) {
    const std::filesystem::path candidates[] = {
        executableDirectory() / "data" / name,
        std::filesystem::path(ORBIS_SOURCE_DATA_DIR) / name,
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::is_directory(candidate, ec)) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace orbis
