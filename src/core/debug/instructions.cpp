#include "core/debug/inspection.hpp"
#include "core/system/console.hpp"

namespace cupid::n64 {

std::optional<std::uint32_t>
Inspection::instruction(const Console &console, DebugProcessor processor, std::uint64_t address) {
  if (address & 3)
    return {};
  if (processor != DebugProcessor::Cpu && processor != DebugProcessor::Rsp)
    return {};
  if (processor == DebugProcessor::Rsp)
    return address <= 0xfff ? std::optional(static_cast<std::uint32_t>(console.rsp_.read_local(
                                  0x1000 | static_cast<std::uint32_t>(address), 4)))
                            : std::nullopt;
  const auto &cpu = console.cpu_;
  const auto access = cpu.inspect_address(address);
  if (!access)
    return {};
  const auto physical = access->physical ^ (cpu.little_endian() ? 4 : 0);
  if (access->cached) {
    const auto &line = cpu.icache_[(address >> 5) & 511];
    if (line.hit(physical))
      return line.words[(physical >> 2) & 7];
  }
  if (physical <= 0x03efffff) {
    const bool ebus = console.mi_.read_word(0) & 0x100;
    if (access->cached && ebus)
      return {};
    return console.ram_.inspect_word(physical, ebus);
  }
  if (access->cached)
    return {};
  if (physical >= 0x04000000 && physical < 0x04002000)
    return static_cast<std::uint32_t>(console.rsp_.read_local(physical & 0x1fff, 4));
  const auto backing = [&](std::uint32_t base,
                           std::span<const std::uint8_t> bytes) -> std::optional<std::uint32_t> {
    if (physical < base || physical - base >= bytes.size() || bytes.size() - (physical - base) < 4)
      return {};
    const auto offset = physical - base;
    return (std::uint32_t(bytes[offset]) << 24) | (std::uint32_t(bytes[offset + 1]) << 16) |
           (std::uint32_t(bytes[offset + 2]) << 8) | bytes[offset + 3];
  };
  if (const auto value = backing(0x10000000, console.rom_.data()))
    return value;
  if (const auto value = backing(0x06000000, console.disk_.ipl_))
    return value;
  if (const auto value = backing(0x1fc00000, console.pif_.rom_))
    return value;
  return backing(0x1fc007c0, console.pif_.ram_);
}

} // namespace cupid::n64
