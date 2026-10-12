#include "core/cpu/cpu.hpp"

namespace cupid::n64 {

std::optional<Address> Cpu::inspect_address(std::uint64_t address, bool write) const {
  if ((address & 3) ||
      (!extended_addressing() && sign_word(static_cast<std::uint32_t>(address)) != address))
    return {};
  if (address >= 0xffffffff80000000ull && address < 0xffffffff83f00000ull)
    return direct_address(Segment::Cached, address);
  const auto region = segment(address);
  if (const auto direct = direct_address(region, address))
    return direct;
  if (region == Segment::Invalid)
    return {};
  const TlbEntry *entry = nullptr;
  for (const auto &lookup : tlb_lookup_)
    if (lookup.entry && tlb_matches(*lookup.entry, address)) {
      entry = lookup.entry;
      break;
    }
  if (!entry)
    for (const auto &candidate : tlb_)
      if (tlb_matches(candidate, address)) {
        entry = &candidate;
        break;
      }
  if (!entry)
    return {};
  const auto offset_mask = (std::uint64_t(entry->mask) | 0x1fff) >> 1;
  const auto lo = entry->lo[(address & (offset_mask + 1)) != 0];
  const auto required = write ? 6u : 2u;
  if ((lo & required) != required)
    return {};
  return tlb_address(*entry, address);
}

} // namespace cupid::n64
