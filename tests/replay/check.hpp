#pragma once

#include "../support/test.hpp"
#include "engine.hpp"
#include <fstream>
#include <string_view>

namespace test::machine_replay {

inline bool check(const Input &input, std::string_view name, const Observer &observe = {}) {
  const auto difference = compare(input, observe);
  equal(difference.has_value(), false);
  if (!difference)
    return true;
  std::cerr << "Fixture " << name << '\n';
  print(*difference);
  const auto reduced = reduce(input, [&](const Input &candidate) {
    const auto current = compare(candidate, observe);
    return current && current->state.field == difference->state.field &&
           current->state.index == difference->state.index;
  });
  const auto bytes = encode(reduced);
  std::ofstream file("replay-failure.bin", std::ios::binary);
  file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
  std::cerr << "Reduced replay: " << reduced.initial.size() << " setup actions, "
            << reduced.actions.size() << " replay actions, " << bytes.size() << " bytes\n";
  return false;
}

inline void code(Input &input, std::span<const std::uint32_t> words) {
  for (unsigned n = 0; n < words.size(); ++n)
    input.initial.push_back({ActionKind::WriteMemory, 0x1000 + n * 4, words[n]});
}

inline void signal_code(Input &input, std::span<const std::uint32_t> words) {
  for (unsigned n = 0; n < words.size(); ++n)
    input.initial.push_back({ActionKind::RspLocal, 0x1000 + n * 4, words[n]});
}

} // namespace test::machine_replay
