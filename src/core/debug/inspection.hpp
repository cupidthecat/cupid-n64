#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cupid::n64 {

class Console;

enum class MemoryRegionId {
  Rdram,
  DataCache,
  RspDmem,
  RspImem,
  PifRam,
  PifRom,
  CartridgeRom,
  Sram,
  Eeprom,
  Flash,
  DiskIpl
};

struct MemoryRegion {
  MemoryRegionId id;
  std::string_view name;
  std::uint32_t size;
  std::optional<std::uint64_t> address;
  bool writable;
  std::string_view access;
};

enum class DeviceId {
  Console,
  Cpu,
  Rsp,
  Rdram,
  Mi,
  Vi,
  Ai,
  Pi,
  Si,
  Ri,
  Rdp,
  Pif,
  Disk,
  Controllers
};

struct DeviceProperty {
  std::string name;
  std::string value;
  bool operator==(const DeviceProperty &) const = default;
};

struct DeviceInfo {
  DeviceId id;
  std::string_view name;
};

// Call between execution intervals, with GPU work fenced before memory access.
class Inspection {
public:
  static constexpr unsigned maximum_transfer = 4096;
  static std::vector<MemoryRegion> regions(const Console &console);
  static std::vector<std::uint8_t> read(const Console &console, MemoryRegionId region,
                                        std::uint32_t offset, unsigned length);
  static void write(Console &console, MemoryRegionId region, std::uint32_t offset,
                    std::span<const std::uint8_t> bytes);
  static std::span<const DeviceInfo> devices();
  static std::vector<DeviceProperty> properties(const Console &console, DeviceId device);
};

} // namespace cupid::n64
