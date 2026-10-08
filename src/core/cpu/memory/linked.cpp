#include "core/cpu/cpu.hpp"

namespace cupid::n64 {

void Cpu::load_linked(unsigned reg, std::uint64_t address, bool wide) {
  if (wide && !require_doubleword())
    return;
  // Mapping faults take precedence over doubleword alignment faults.
  const auto access = translate(address, 4, false);
  if (!access)
    return;
  const auto data = read(address, wide ? 8 : 4);
  if (!data)
    return;
  state_.gpr[reg] = wide ? *data : sign_word(static_cast<std::uint32_t>(*data));
  control_[LlAddr] = access->physical >> 4;
  llbit_ = true;
}

} // namespace cupid::n64
