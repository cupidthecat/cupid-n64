#pragma once

#include <cwctype>
#include <string>
#include <string_view>

namespace cupid::desktop {

inline std::wstring audio_endpoint_key(std::wstring_view value) {
  // Saved device-interface paths embed the stable endpoint ID between two braces.
  const auto first = value.find(L"{0.0.");
  if (first != std::wstring_view::npos) {
    const auto end = value.find(L'}', first);
    const auto last = end == std::wstring_view::npos ? end : value.find(L'}', end + 1);
    if (last != std::wstring_view::npos)
      value = value.substr(first, last - first + 1);
  }
  return std::wstring(value);
}

inline bool same_audio_endpoint(std::wstring_view left, std::wstring_view right) {
  const auto a = audio_endpoint_key(left), b = audio_endpoint_key(right);
  if (a.size() != b.size())
    return false;
  for (std::size_t n = 0; n < a.size(); ++n)
    if (std::towlower(a[n]) != std::towlower(b[n]))
      return false;
  return true;
}

} // namespace cupid::desktop
