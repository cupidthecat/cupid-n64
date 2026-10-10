#include "core/state/core_state.hpp"
#include <algorithm>

namespace cupid::n64 {

std::optional<StateDifference> StateCheckpoint::difference(const StateCheckpoint &actual) const {
  const auto valid = [](const StateCheckpoint &checkpoint) {
    std::size_t end = 0;
    for (const auto &range : checkpoint.ranges) {
      if (range.name.empty() || range.offset != end || !range.element_bytes ||
          range.element_bytes > 8 || range.offset > checkpoint.bytes.size() ||
          range.bytes > checkpoint.bytes.size() - range.offset || range.bytes % range.element_bytes)
        return false;
      end += range.bytes;
    }
    return checkpoint.bytes.size() >= 8 && end == checkpoint.bytes.size() - 8;
  };
  if (!valid(*this) || !valid(actual))
    return StateDifference{"layout.invalid", 0, valid(*this), valid(actual), 0};
  if (ranges.size() != actual.ranges.size())
    return StateDifference{"layout.ranges", 0, ranges.size(), actual.ranges.size(), 0};
  for (std::size_t n = 0; n < ranges.size(); ++n) {
    const auto &left = ranges[n], &right = actual.ranges[n];
    if (left.name != right.name || left.element_bytes != right.element_bytes)
      return StateDifference{"layout.field", n, 0, 0, 0};
    if (left.bytes != right.bytes)
      return StateDifference{left.name + ".bytes", 0, left.bytes, right.bytes, 0};
    if (std::equal(bytes.begin() + left.offset, bytes.begin() + left.offset + left.bytes,
                   actual.bytes.begin() + right.offset))
      continue;
    for (std::size_t offset = 0; offset < left.bytes; offset += left.element_bytes) {
      const auto width = std::min<std::size_t>(left.element_bytes, left.bytes - offset);
      std::uint64_t expected = 0, value = 0;
      for (unsigned byte = 0; byte < width; ++byte) {
        expected |= std::uint64_t(bytes[left.offset + offset + byte]) << (byte * 8);
        value |= std::uint64_t(actual.bytes[right.offset + offset + byte]) << (byte * 8);
      }
      if (expected != value)
        return StateDifference{left.name, offset / left.element_bytes, expected, value,
                               static_cast<unsigned>(width)};
    }
  }
  return {};
}

} // namespace cupid::n64
