#include "core/debug/debugger.hpp"
#include "core/debug/inspection.hpp"
#include "core/system/console.hpp"
#include <limits>

namespace cupid::n64 {

std::optional<std::uint32_t> Debugger::io_word(std::uint32_t physical) const {
  const auto index = (physical & 31) >> 2;
  switch (physical & 0xfff00000) {
  case 0x04000000:
    if (physical >= 0x04040000 && physical < 0x04080000)
      return console_.signal().peek_io(physical);
    if (physical >= 0x04080000 && physical < 0x040c0000)
      return (physical & 7) == 0 ? console_.signal().pc() : 0;
    return {};
  case 0x04100000:
    return console_.display().peek_word(physical);
  case 0x04200000:
    return console_.display().read_test(physical);
  case 0x04300000:
    return console_.interrupts().read_word(physical);
  case 0x04400000:
    return console_.video().peek_word(physical);
  case 0x04500000:
    return index == 3 ? console_.audio().status() : console_.audio().state().lengths[0];
  case 0x04600000:
    return console_.pi_.read_io(physical);
  case 0x04700000:
    return console_.ri_.read_word(physical);
  case 0x04800000:
    return console_.si_.peek_io(physical);
  default:
    return {};
  }
}

std::optional<std::vector<std::uint8_t>> Debugger::read_memory(std::uint64_t address,
                                                               unsigned length) {
  if (!attached_ || !halted_ || length > maximum_transfer)
    return {};
  address = canonical(address);
  if (length && length - 1 > std::numeric_limits<std::uint64_t>::max() - address)
    return {};
  std::vector<std::uint8_t> result;
  result.reserve(length);
  const auto regions = Inspection::regions(console_);
  for (unsigned n = 0; n < length; ++n) {
    const auto current = address + n;
    if (const auto value = Inspection::data_byte(console_, DebugProcessor::Cpu, current)) {
      result.push_back(*value);
      continue;
    }
    const auto access = console_.cpu().inspect_address(current & ~3ull);
    if (!access || access->cached)
      return {};
    const auto physical = (access->physical | static_cast<unsigned>(current & 3)) ^
                          (console_.cpu().little_endian() ? 7u : 0u);
    if (const auto value = io_word(physical & ~3u)) {
      result.push_back(static_cast<std::uint8_t>(*value >> ((3 - (physical & 3)) * 8)));
      continue;
    }
    bool found = false;
    for (const auto &region : regions)
      if (region.address && physical >= *region.address &&
          physical - *region.address < region.size) {
        const auto byte = Inspection::read(
            console_, region.id, static_cast<std::uint32_t>(physical - *region.address), 1);
        result.push_back(byte[0]);
        found = true;
        break;
      }
    if (!found)
      return {};
  }
  return result;
}

bool Debugger::write_memory(std::uint64_t address, std::span<const std::uint8_t> bytes) {
  if (!attached_ || !halted_ || bytes.size() > maximum_transfer)
    return false;
  address = canonical(address);
  if (!bytes.empty() && bytes.size() - 1 > std::numeric_limits<std::uint64_t>::max() - address)
    return false;
  struct ByteTarget {
    std::uint32_t physical;
    unsigned cache;
    std::optional<MemoryRegionId> region;
    std::uint32_t offset;
  };
  std::vector<ByteTarget> targets;
  targets.reserve(bytes.size());
  const auto regions = Inspection::regions(console_);
  auto &cpu = console_.cpu();
  for (unsigned n = 0; n < bytes.size(); ++n) {
    const auto current = address + n;
    const auto access = cpu.inspect_address(current & ~3ull, true);
    if (!access)
      return false;
    const auto physical =
        (access->physical | static_cast<unsigned>(current & 3)) ^ (cpu.little_endian() ? 7u : 0u);
    const auto cache = static_cast<unsigned>((current >> 4) & 511);
    if (physical <= 0x03efffff) {
      if (!Inspection::data_byte(console_, DebugProcessor::Cpu, current))
        return false;
      targets.push_back({physical, access->cached ? cache : 512, {}, 0});
      continue;
    }
    if (access->cached)
      return false;
    if (io_word(physical & ~3u)) {
      if (bytes.size() != 4 || (address & 3) || n)
        return false;
      std::uint32_t value = 0;
      for (unsigned byte = 0; byte < bytes.size(); ++byte)
        value |= std::uint32_t(bytes[byte]) << ((cpu.little_endian() ? byte : 3 - byte) * 8);
      console_.write(physical & ~3u, 4, value);
      return true;
    }
    bool found = false;
    for (const auto &region : regions)
      if (region.writable && region.address && physical >= *region.address &&
          physical - *region.address < region.size) {
        targets.push_back(
            {physical, 512, region.id, static_cast<std::uint32_t>(physical - *region.address)});
        found = true;
        break;
      }
    if (!found)
      return false;
  }
  for (unsigned n = 0; n < targets.size(); ++n) {
    const auto &target = targets[n];
    if (target.cache < 512 && cpu.dcache_[target.cache].hit(target.physical)) {
      auto &line = cpu.dcache_[target.cache];
      auto &word = line.words[(target.physical >> 2) & 3];
      const auto shift = (3 - (target.physical & 3)) * 8;
      word = (word & ~(255u << shift)) | (std::uint32_t(bytes[n]) << shift);
      console_.diagnostics().cache_dirty(target.cache, target.physical,
                                         static_cast<std::uint16_t>(1u << (target.physical & 15)),
                                         0);
      line.dirty = true;
    } else if (target.region)
      Inspection::write(console_, *target.region, target.offset, bytes.subspan(n, 1));
    else
      console_.ram().write(target.physical, 1, bytes[n],
                           (console_.interrupts().read_word(0) & 0x100) != 0,
                           MemoryRequestor::Host);
  }
  return true;
}

} // namespace cupid::n64
