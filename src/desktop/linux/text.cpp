#include "desktop/linux/text.hpp"
#include <glib.h>
#include <stdexcept>
#include <vector>

namespace cupid::desktop::linux_host {

std::wstring wide(std::string_view text) {
  if (text.empty())
    return {};
  glong length = 0;
  auto characters =
      g_utf8_to_ucs4(text.data(), static_cast<glong>(text.size()), nullptr, &length, nullptr);
  if (!characters)
    throw std::runtime_error("The selected name is not valid UTF-8.");
  std::wstring result(characters, characters + length);
  g_free(characters);
  return result;
}

std::string utf8(std::wstring_view text) {
  if (text.empty())
    return {};
  const std::vector<gunichar> characters(text.begin(), text.end());
  auto encoded = g_ucs4_to_utf8(characters.data(), static_cast<glong>(characters.size()), nullptr,
                                nullptr, nullptr);
  if (!encoded)
    throw std::runtime_error("The selected name contains an invalid character.");
  std::string result(encoded);
  g_free(encoded);
  return result;
}

} // namespace cupid::desktop::linux_host
