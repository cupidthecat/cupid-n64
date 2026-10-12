#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cupid::desktop::settings {

struct NameLess {
  bool operator()(std::wstring_view first, std::wstring_view second) const;
};

using Section = std::map<std::wstring, std::wstring, NameLess>;
using Document = std::map<std::wstring, Section, NameLess>;
inline constexpr std::size_t maximum_size = 4 * 1024 * 1024;

Document read_document(const std::filesystem::path &path, bool exported);
std::vector<std::uint8_t> encode_document(const Document &document, bool exported);

} // namespace cupid::desktop::settings
