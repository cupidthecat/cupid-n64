#pragma once

#include "desktop/controls/input.hpp"
#include <filesystem>
#include <istream>
#include <vector>

namespace cupid::desktop {

class InputReplay {
public:
  explicit InputReplay(std::istream &stream);
  explicit InputReplay(const std::filesystem::path &path);
  PortInput at(unsigned field) const;

private:
  struct Range {
    unsigned first, last;
    PortInput input;
  };
  void load(std::istream &stream);
  std::vector<Range> ranges_;
};

} // namespace cupid::desktop
