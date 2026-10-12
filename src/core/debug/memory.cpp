#include "core/debug/inspection.hpp"
#include "core/system/console.hpp"
#include <algorithm>
#include <stdexcept>

namespace cupid::n64 {
namespace {

void range(const std::vector<MemoryRegion> &regions, MemoryRegionId id, std::uint32_t offset,
           std::size_t length, bool write) {
  const auto found = std::find_if(regions.begin(), regions.end(),
                                  [id](const auto &region) { return region.id == id; });
  if (found == regions.end())
    throw std::runtime_error("This memory region is unavailable.");
  if (write && !found->writable)
    throw std::runtime_error("This memory region is read-only.");
  if (length > Inspection::maximum_transfer || offset > found->size ||
      length > found->size - offset)
    throw std::runtime_error("The requested bytes are outside the selected memory region.");
}

} // namespace

std::vector<MemoryRegion> Inspection::regions(const Console &console) {
  using Id = MemoryRegionId;
  std::vector<MemoryRegion> result{
      {Id::Rdram, "RDRAM", console.ram_.size(), 0, true,
       "Direct backing memory; CPU caches are bypassed"},
      {Id::DataCache, "CPU cached RDRAM", console.ram_.size(), 0xffffffff80000000ull, true,
       "KSEG0 data-cache hits; direct backing memory on misses; no cache fills"},
      {Id::RspDmem, "RSP DMEM", 4096, 0x04000000, true, "Direct memory; no device bus access"},
      {Id::RspImem, "RSP IMEM", 4096, 0x04001000, true,
       "Direct memory; writes invalidate compiled RSP code"},
      {Id::PifRam, "PIF RAM", 64, 0x1fc007c0, true, "Direct memory; no PIF commands are executed"},
      {Id::PifRom, "PIF ROM", 0x7c0, 0x1fc00000, false, "Direct firmware bytes"}};
  const auto add = [&](Id id, std::string_view name, std::size_t size,
                       std::optional<std::uint64_t> address, bool writable) {
    if (size)
      result.push_back({id, name, static_cast<std::uint32_t>(size), address, writable,
                        "Direct memory; no cartridge bus access"});
  };
  add(Id::CartridgeRom, "Cartridge ROM", console.rom_.data().size(), 0x10000000, false);
  add(Id::Sram, "Cartridge SRAM", console.sram_.data().size(), 0x08000000, true);
  add(Id::Eeprom, "Cartridge EEPROM", console.eeprom_.data().size(), {}, true);
  add(Id::Flash, "Cartridge Flash", console.flash_.data().size(), 0x08000000, true);
  add(Id::DiskIpl, "64DD IPL", console.disk_.ipl_.size(), 0x06000000, false);
  return result;
}

std::vector<std::uint8_t> Inspection::read(const Console &console, MemoryRegionId region,
                                           std::uint32_t offset, unsigned length) {
  range(regions(console), region, offset, length, false);
  std::vector<std::uint8_t> result(length);
  for (unsigned n = 0; n < length; ++n) {
    const auto address = offset + n;
    std::uint64_t value = 0;
    switch (region) {
    case MemoryRegionId::DataCache: {
      const auto &line = console.cpu_.dcache_[(address >> 4) & 511];
      value = line.hit(address) ? (line.words[(address >> 2) & 3] >> ((3 - (address & 3)) * 8))
                                : console.ram_.read_raw(address, 1);
      break;
    }
    case MemoryRegionId::Rdram:
      value = console.ram_.read_raw(address, 1);
      break;
    case MemoryRegionId::RspDmem:
      value = console.rsp_.read_local(address, 1);
      break;
    case MemoryRegionId::RspImem:
      value = console.rsp_.read_local(address + 4096, 1);
      break;
    case MemoryRegionId::PifRam:
      value = console.pif_.ram_[address];
      break;
    case MemoryRegionId::PifRom:
      value = console.pif_.rom_[address];
      break;
    case MemoryRegionId::CartridgeRom:
      value = console.rom_.data()[address];
      break;
    case MemoryRegionId::Sram:
      value = console.sram_.data()[address];
      break;
    case MemoryRegionId::Eeprom:
      value = console.eeprom_.data()[address];
      break;
    case MemoryRegionId::Flash:
      value = console.flash_.data()[address];
      break;
    case MemoryRegionId::DiskIpl:
      value = console.disk_.ipl_[address];
      break;
    }
    result[n] = static_cast<std::uint8_t>(value);
  }
  return result;
}

void Inspection::write(Console &console, MemoryRegionId region, std::uint32_t offset,
                       std::span<const std::uint8_t> bytes) {
  range(regions(console), region, offset, bytes.size(), true);
  for (unsigned n = 0; n < bytes.size(); ++n) {
    const auto address = offset + n;
    switch (region) {
    case MemoryRegionId::DataCache: {
      auto &line = console.cpu_.dcache_[(address >> 4) & 511];
      if (line.hit(address)) {
        console.diagnostics_.cache_dirty((address >> 4) & 511, address,
                                         static_cast<std::uint16_t>(1u << (address & 15)), 0);
        auto &word = line.words[(address >> 2) & 3];
        const auto shift = (3 - (address & 3)) * 8;
        word = (word & ~(255u << shift)) | (std::uint32_t(bytes[n]) << shift);
        line.dirty = true;
        break;
      }
      console.ram_.write_raw(address, 1, bytes[n]);
      console.ram_.update_hidden(address, 1, bytes[n], false);
      break;
    }
    case MemoryRegionId::Rdram:
      console.ram_.write_raw(address, 1, bytes[n]);
      console.ram_.update_hidden(address, 1, bytes[n], false);
      break;
    case MemoryRegionId::RspDmem:
      console.rsp_.write_local(address, 1, bytes[n]);
      break;
    case MemoryRegionId::RspImem:
      console.rsp_.write_local(address + 4096, 1, bytes[n]);
      break;
    case MemoryRegionId::PifRam:
      console.pif_.ram_[address] = bytes[n];
      break;
    case MemoryRegionId::Sram:
      console.sram_.data()[address] = bytes[n];
      break;
    case MemoryRegionId::Eeprom:
      console.eeprom_.data()[address] = bytes[n];
      break;
    case MemoryRegionId::Flash:
      console.flash_.data()[address] = bytes[n];
      break;
    default:
      throw std::runtime_error("This memory region is read-only.");
    }
  }
}

} // namespace cupid::n64
