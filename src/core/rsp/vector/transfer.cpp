#include "core/rsp/rsp.hpp"

namespace cupid::n64 {

void Rsp::vector_transfer(std::uint32_t instruction) {
  const auto operation = (instruction >> 21) & 31;
  const auto target = (instruction >> 16) & 31;
  const auto source = (instruction >> 11) & 31;
  const auto element = (instruction >> 7) & 15;
  auto &value = state_.gpr[target];
  auto &vector = state_.vectors[source];
  if (operation == 0 && target) {
    const auto half = (unsigned(vector.byte(element)) << 8) | vector.byte((element + 1) & 15);
    value = static_cast<std::uint32_t>(static_cast<std::int16_t>(half));
  }
  if (operation == 4) {
    vector.byte(element, static_cast<std::uint8_t>(value >> 8));
    if (element != 15)
      vector.byte(element + 1, static_cast<std::uint8_t>(value));
  }
  if (operation == 2 && target) {
    std::uint16_t flags = state_.extension;
    if ((source & 3) == 0)
      flags = static_cast<std::uint16_t>(state_.carry_low | (unsigned(state_.carry_high) << 8));
    if ((source & 3) == 1)
      flags = static_cast<std::uint16_t>(state_.compare_low | (unsigned(state_.compare_high) << 8));
    value = static_cast<std::uint32_t>(static_cast<std::int16_t>(flags));
  }
  if (operation == 6) {
    if ((source & 3) == 0) {
      state_.carry_low = static_cast<std::uint8_t>(value);
      state_.carry_high = static_cast<std::uint8_t>(value >> 8);
    } else if ((source & 3) == 1) {
      state_.compare_low = static_cast<std::uint8_t>(value);
      state_.compare_high = static_cast<std::uint8_t>(value >> 8);
    } else
      state_.extension = static_cast<std::uint8_t>(value);
  }
}

} // namespace cupid::n64
