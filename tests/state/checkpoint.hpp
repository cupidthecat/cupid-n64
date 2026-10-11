#pragma once

#include "../support/test.hpp"
#include "core/state/core_state.hpp"
#include <unordered_set>

namespace test {

inline void checkpoint_layout(const cupid::n64::StateCheckpoint &checkpoint) {
  std::unordered_set<std::string> names;
  std::size_t end = 0;
  for (const auto &range : checkpoint.ranges) {
    equal(range.offset, end);
    equal(!range.name.empty(), true);
    const auto unique = names.insert(range.name).second;
    if (!unique)
      std::cerr << "Repeated checkpoint field " << range.name << '\n';
    equal(unique, true);
    equal(range.element_bytes > 0 && range.element_bytes <= 8, true);
    if (range.element_bytes)
      equal(range.bytes % range.element_bytes, 0);
    end += range.bytes;
  }
  equal(end + 8, checkpoint.bytes.size());
}

template <typename Include>
unsigned checkpoint_faults(const cupid::n64::StateCheckpoint &checkpoint, Include include) {
  auto actual = checkpoint;
  unsigned tested = 0;
  for (const auto &range : checkpoint.ranges) {
    if (!include(range.name) || !range.bytes)
      continue;
    ++tested;
    for (const auto byte : {std::size_t(0), range.bytes - 1}) {
      actual.bytes[range.offset + byte] ^= 1;
      const auto difference = checkpoint.difference(actual);
      equal(difference.has_value(), true);
      if (difference) {
        equal(difference->field == range.name, true);
        equal(difference->index, byte / range.element_bytes);
        equal(difference->bytes, range.element_bytes);
        equal(difference->expected ^ difference->actual, std::uint64_t(1)
                                                             << ((byte % range.element_bytes) * 8));
      }
      actual.bytes[range.offset + byte] ^= 1;
    }
  }
  return tested;
}

} // namespace test
