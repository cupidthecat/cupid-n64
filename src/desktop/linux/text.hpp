#pragma once

#include <string>
#include <string_view>

namespace cupid::desktop::linux_host {

std::wstring wide(std::string_view text);
std::string utf8(std::wstring_view text);

} // namespace cupid::desktop::linux_host
