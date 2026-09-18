#include "rocket_volley/SaveFile.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

int main() {
    namespace fs = std::filesystem;
    int failures = 0;
    const auto check = [&](bool condition, const char *name) {
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        if (!condition) ++failures;
    };
    const auto read = [](const fs::path &path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    // Only newly created directories owned by this test are ever cleaned up.
    const auto root = fs::current_path() / ("save-test-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(root)) return 1;
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code error; fs::remove_all(path, error); }
    } cleanup{root};

    const auto save = root / "profile" / "settings.cfg";
    const std::string original = "version=3\ncareer_xp=120\n";
    const std::string newer = "version=3\ncareer_xp=240\n";
    check(rv::writeSaveFile(save, original) && read(save) == original, "first save creates missing profile directory");
    check(rv::writeSaveFile(save, newer) && read(save) == newer, "replacement retains exact compatible settings bytes");
    const std::string ghost("RVGHOST1\0payload\n", 17);
    const auto ghostPath = root / "profile" / "academy_best.ghost";
    check(rv::writeSaveFile(ghostPath, ghost) && read(ghostPath) == ghost, "ghost data is written without byte conversion");

    // A path occupied by a directory forces failure at the final replacement,
    // after staging has succeeded. Existing contents must survive intact.
    const auto blocked = root / "occupied";
    fs::create_directory(blocked);
    const auto marker = blocked / "keep.cfg";
    check(rv::writeSaveFile(marker, original), "prepare replacement failure fixture");
    check(!rv::writeSaveFile(blocked, newer) && read(marker) == original,
        "failed replacement preserves existing destination contents");
    check(!rv::writeSaveFile(save / "child.cfg", newer) && read(save) == newer,
        "unwritable parent leaves earlier save untouched");

#ifdef _WIN32
    // Antivirus/editors may permit reading but temporarily deny replacement.
    HANDLE locked = CreateFileW(save.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "prepare Windows locked-save fixture");
    if (locked != INVALID_HANDLE_VALUE) {
        check(!rv::writeSaveFile(save, original) && read(save) == newer,
            "locked save reports failure and preserves previous progress");
        CloseHandle(locked);
    }
    check(rv::writeSaveFile(save, original) && read(save) == original,
        "saving recovers after temporary Windows file lock");
#endif

    bool noStagingFiles = true;
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
        noStagingFiles = noStagingFiles && entry.path().filename().string().find(".tmp.") == std::string::npos;
    }
    check(noStagingFiles, "successful and failed saves clean up their staging files");
    check(!rv::writeSaveFile({}, newer), "empty destination is rejected");
    return failures == 0 ? 0 : 1;
}
