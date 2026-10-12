#include "core/cpu/cpu.hpp"
#include <algorithm>

namespace cupid::n64 {

bool Cpu::tlb_matches(const TlbEntry &entry, std::uint64_t address) const {
  const bool global = (entry.lo[0] & entry.lo[1] & 1) != 0;
  const auto mask = 0x000000ffffffffffull & ~(std::uint64_t(entry.mask) | 0x1fff);
  return (global || (entry.hi & 0xff) == (control_[EntryHi] & 0xff)) &&
         (address >> 62) == (entry.hi >> 62) && (address & mask) == (entry.hi & mask);
}

std::optional<Address> Cpu::translate_tlb(std::uint64_t address, bool store) {
  const TlbEntry *entry = nullptr;
  for (auto &lookup : tlb_lookup_) {
    if (lookup.entry && tlb_matches(*lookup.entry, address)) {
      ++lookup.frequency;
      entry = lookup.entry;
      break;
    }
  }
  if (!entry) {
    for (const auto &candidate : tlb_) {
      if (!tlb_matches(candidate, address))
        continue;
      const auto slot = std::min_element(tlb_lookup_.begin(), tlb_lookup_.end(),
                                         [](const auto &a, const auto &b) {
                                           // Preserve signed ordering without signed overflow.
                                           return std::bit_cast<std::int32_t>(a.frequency) <
                                                  std::bit_cast<std::int32_t>(b.frequency);
                                         });
      *slot = {&candidate, 0};
      entry = &candidate;
      break;
    }
  }
  if (!entry) {
    trace_tlb(TlbEvent::Miss, address);
    address_exception(address);
    raise(store ? Exception::TlbStore : Exception::TlbLoad, 0, true);
    return {};
  }
  const auto offset_mask = (std::uint64_t(entry->mask) | 0x1fff) >> 1;
  const auto selected = (address & (offset_mask + 1)) != 0;
  const auto lo = entry->lo[selected];
  if (!(lo & 2) || (store && !(lo & 4))) {
    trace_tlb(!(lo & 2) ? TlbEvent::Invalid : TlbEvent::Modification, address);
    address_exception(address);
    raise(!(lo & 2) ? (store ? Exception::TlbStore : Exception::TlbLoad)
                    : Exception::TlbModification);
    return {};
  }
  const auto translated = tlb_address(*entry, address);
  trace_tlb(store ? TlbEvent::Store : TlbEvent::Load, address, translated.physical);
  return translated;
}

Address Cpu::tlb_address(const TlbEntry &entry, std::uint64_t address) {
  const auto offset_mask = (std::uint64_t(entry.mask) | 0x1fff) >> 1;
  const auto lo = entry.lo[(address & (offset_mask + 1)) != 0];
  const auto physical =
      ((std::uint64_t(lo & 0x3fffffc0) << 6) & 0xffffffff) + (address & offset_mask);
  return {static_cast<std::uint32_t>(physical), ((lo >> 3) & 7) != 2};
}

} // namespace cupid::n64
