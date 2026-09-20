#pragma once

#include "cupid/types.hpp"

#include <filesystem>
#include <span>
#include <string>

namespace cupid::storage {

[[nodiscard]] bool replace_file(const std::filesystem::path& path, std::span<const u8> bytes,
                                std::string& error);

} // namespace cupid::storage
