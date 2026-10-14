#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace cupid::desktop {

struct DipSelection {
  std::string name, value;
};

inline DipSelection parse_dip_selection(std::string_view text) {
  const auto split = text.find('=');
  if (split == text.npos || split == 0 || split + 1 == text.size())
    throw std::runtime_error("--dip requires NAME=VALUE.");
  return {std::string(text.substr(0, split)), std::string(text.substr(split + 1))};
}

} // namespace cupid::desktop
