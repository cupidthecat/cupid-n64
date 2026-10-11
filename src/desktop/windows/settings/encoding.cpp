#include "desktop/windows/settings/encoding.hpp"
#include "desktop/storage/save.hpp"
#include <stdexcept>
#include <windows.h>

namespace cupid::desktop {

void unicode_settings(const std::filesystem::path &path) {
  if (path.empty())
    return;
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
  auto bytes = std::filesystem::exists(path) ? read_file(path, 4 * 1024 * 1024)
                                             : std::vector<std::uint8_t>{};
  if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe)
    return;
  const bool utf8 = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf;
  if (utf8)
    bytes.erase(bytes.begin(), bytes.begin() + 3);
  std::wstring text;
  if (!bytes.empty()) {
    const auto codepage = utf8 ? CP_UTF8 : CP_ACP;
    const auto flags = utf8 ? MB_ERR_INVALID_CHARS : 0;
    const auto data = reinterpret_cast<const char *>(bytes.data());
    const auto length = static_cast<int>(bytes.size());
    const auto count = MultiByteToWideChar(codepage, flags, data, length, nullptr, 0);
    if (!count)
      throw std::runtime_error("Could not read the settings file encoding.");
    text.resize(static_cast<std::size_t>(count));
    if (MultiByteToWideChar(codepage, flags, data, length, text.data(), count) != count)
      throw std::runtime_error("Could not read the settings file encoding.");
  }
  std::vector<std::uint8_t> encoded{0xff, 0xfe};
  encoded.reserve(2 + text.size() * 2);
  for (const auto character : text) {
    encoded.push_back(static_cast<std::uint8_t>(character));
    encoded.push_back(static_cast<std::uint8_t>(character >> 8));
  }
  write_save(path, encoded);
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
}

} // namespace cupid::desktop
