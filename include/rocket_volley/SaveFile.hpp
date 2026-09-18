#pragma once

#include <filesystem>
#include <string_view>

namespace rv {

// Stage and flush beside the destination, then replace it without deleting the
// previous save first. On failure, the previous file remains available.
// Atomic per file; this is not a transaction across settings and ghost files.
[[nodiscard]] bool writeSaveFile(const std::filesystem::path &path, std::string_view contents);

} // namespace rv
