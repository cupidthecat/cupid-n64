#include "desktop/replay/input.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace cupid::desktop {

InputReplay::InputReplay(std::istream &stream) {
  load(stream);
}

InputReplay::InputReplay(const std::filesystem::path &path) {
  std::ifstream stream(path);
  if (!stream)
    throw std::runtime_error("Could not open the input replay.");
  load(stream);
}

void InputReplay::load(std::istream &stream) {
  std::string line;
  unsigned number = 0;
  while (std::getline(stream, line)) {
    ++number;
    if (line.find_first_not_of(" \t\r") == std::string::npos)
      continue;
    std::istringstream values(line);
    std::int64_t first, last, buttons, x, y;
    std::string extra;
    if (!(values >> first >> last >> buttons >> x >> y) || (values >> extra) || first < 0 ||
        last <= first || last > std::numeric_limits<unsigned>::max() || buttons < 0 ||
        buttons > 65535 || x < -128 || x > 127 || y < -128 || y > 127 ||
        (!ranges_.empty() && first < ranges_.back().last))
      throw std::runtime_error("Invalid or overlapping input replay range on line " +
                               std::to_string(number) + ".");
    PortInput input;
    input.buttons = static_cast<std::uint16_t>(buttons);
    input.x = static_cast<std::int16_t>(x);
    input.y = static_cast<std::int16_t>(y);
    input.calibrated_stick = true;
    ranges_.push_back({static_cast<unsigned>(first), static_cast<unsigned>(last), input});
  }
  if (!stream.eof())
    throw std::runtime_error("Could not read the input replay.");
}

PortInput InputReplay::at(unsigned field) const {
  const auto next =
      std::upper_bound(ranges_.begin(), ranges_.end(), field,
                       [](unsigned value, const Range &range) { return value < range.first; });
  if (next != ranges_.begin()) {
    const auto &range = *std::prev(next);
    if (field < range.last)
      return range.input;
  }
  PortInput neutral;
  neutral.calibrated_stick = true;
  return neutral;
}

} // namespace cupid::desktop
