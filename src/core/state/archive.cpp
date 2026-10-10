#include "core/state/archive.hpp"

namespace cupid::n64::state {

Archive::Archive(std::span<const std::uint8_t> input) : loading_(true) {
  require(input.size() >= 8);
  input_ = input.last(8);
  const auto checksum = literal<std::uint64_t>(0);
  input_ = input.first(input.size() - 8);
  position_ = 0;
  require(fingerprint(input_) == checksum);
}

std::uint64_t Archive::fingerprint(std::span<const std::uint8_t> bytes) {
  std::uint64_t value = 0xcbf29ce484222325ull;
  for (const auto byte : bytes)
    value = (value ^ byte) * 0x100000001b3ull;
  return value;
}

void Archive::require(bool condition) {
  if (!condition)
    throw InvalidState("Invalid or incompatible machine state");
}

std::vector<std::uint8_t> Archive::finish() {
  if (loading_) {
    validate();
    for (auto &mutation : mutations_)
      mutation();
    mutations_.clear();
    return {};
  }
  literal(fingerprint(output_));
  return std::move(output_);
}

void Archive::validate() const {
  require(loading_ && position_ == input_.size());
}

} // namespace cupid::n64::state
