#include "rocket_volley/SaveFile.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace rv {

bool writeSaveFile(const std::filesystem::path &path, std::string_view contents) {
    if (path.filename().empty()) return false;
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return false;
    }

    // Exclusive creation prevents concurrent game instances from sharing a
    // staging file. A leftover staging file never replaces a committed save.
    static std::atomic<unsigned long long> sequence{0};
    std::filesystem::path temporary;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    const auto process = GetCurrentProcessId();
#else
    int file = -1;
    const auto process = getpid();
#endif
    for (int attempt = 0; attempt < 32; ++attempt) {
        temporary = path;
        temporary += ".tmp." + std::to_string(process) + "." + std::to_string(sequence.fetch_add(1));
#ifdef _WIN32
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) return false;
#else
        file = open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (file >= 0) break;
        if (errno != EEXIST) return false;
#endif
    }
#ifdef _WIN32
    if (file == INVALID_HANDLE_VALUE) return false;
#else
    if (file < 0) return false;
#endif

    bool complete = true;
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const auto count = static_cast<unsigned>(std::min<std::size_t>(contents.size() - offset, 1024 * 1024));
#ifdef _WIN32
        DWORD written = 0;
        if (!WriteFile(file, contents.data() + offset, count, &written, nullptr) || written == 0) {
#else
        const auto written = write(file, contents.data() + offset, count);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
#endif
            complete = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
#ifdef _WIN32
    if (complete && !FlushFileBuffers(file)) complete = false;
    if (!CloseHandle(file)) complete = false;
    if (complete) complete = MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (complete && fsync(file) != 0) complete = false;
    if (close(file) != 0) complete = false;
    if (complete) {
        std::filesystem::rename(temporary, path, error);
        complete = !error;
    }
#endif
    if (!complete) std::filesystem::remove(temporary, error);
    return complete;
}

} // namespace rv
