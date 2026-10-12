#include "desktop/debug/format.hpp"
#include <algorithm>
#include <array>
#include <iomanip>
#include <ranges>
#include <sstream>

namespace cupid::desktop {
namespace {

constexpr std::array<std::string_view, 12> devices{
    "MI", "VI", "Audio", "PI", "SI", "RI", "RDRAM", "PIF", "RDP", "RSP", "Cartridge", "Disk"};
constexpr std::array<std::string_view, 10> requestors{
    "Instruction cache", "Data cache", "CPU uncached", "SP DMA",          "PI DMA", "SI DMA",
    "Display draw",      "Audio DMA",  "Video DMA",    "Display commands"};

void origin(std::ostream &out, n64::DiagnosticOrigin context) {
  if (!context.known) {
    out << "unknown origin";
    return;
  }
  out << (context.processor == n64::DebugProcessor::Rsp ? "RSP PC " : "CPU PC ") << std::hex
      << std::setfill('0') << std::setw(context.processor == n64::DebugProcessor::Rsp ? 3 : 16)
      << context.pc;
}

std::string row(const n64::DiagnosticRecord &record) {
  std::ostringstream out;
  out << std::dec << record.sequence << "  ";
  origin(out, record.origin);
  out << "  ";
  using n64::DiagnosticKind;
  if (record.kind == DiagnosticKind::HomebrewLog) {
    out << "EMUX ";
    for (unsigned char byte : record.text)
      if (byte == '\n')
        out << "\r\n";
      else if (byte >= 32 && byte < 127)
        out << char(byte);
      else
        out << "\\x" << std::hex << std::setfill('0') << std::setw(2) << unsigned(byte);
  } else if (record.kind == DiagnosticKind::Exception)
    out << "exception " << std::dec << record.code << (record.value ? " [delay slot]" : "");
  else if (record.kind == DiagnosticKind::HostControl)
    out << "host control " << std::dec << record.code;
  else {
    const auto device = static_cast<unsigned>(record.device);
    out << (device < devices.size() ? devices[device] : "Device") << " 0x" << std::hex
        << std::setw(8) << record.address;
    if (record.kind == DiagnosticKind::RegisterRead || record.kind == DiagnosticKind::RegisterWrite)
      out << (record.kind == DiagnosticKind::RegisterRead ? " => 0x" : " <= 0x")
          << std::setw(record.bytes * 2) << record.value;
    else if (record.kind == DiagnosticKind::RefreshDisabled)
      out << " automatic refresh disabled";
    else {
      out << (record.kind == DiagnosticKind::DirtyRead     ? " dirty cache data read"
              : record.kind == DiagnosticKind::CachedWrite ? " write to cached memory"
                                                           : " incoherent DMEM data read");
      const auto requestor = static_cast<unsigned>(record.requestor);
      out << " by " << (requestor < requestors.size() ? requestors[requestor] : "host") << " ("
          << std::dec << record.bytes << " bytes)";
      if (record.kind == DiagnosticKind::DmemRead)
        out << " from RDRAM 0x" << std::hex << std::setw(8) << record.related_address;
      out << "\r\n    DMA: ";
      origin(out, record.dma);
      out << "; cache fill PC " << std::hex << std::setw(16) << record.fill_pc
          << "; cache write PC " << std::setw(16) << record.write_pc;
      if (!record.fill_pc || !record.write_pc)
        out << " (zero means unavailable)";
    }
  }
  out << "\r\n";
  return out.str();
}

} // namespace

std::string diagnostic_dump(std::span<const n64::DiagnosticRecord> records, unsigned limit) {
  const auto count = std::min<std::size_t>({records.size(), limit, 256});
  std::vector<std::string> rows;
  unsigned bytes = 0;
  for (const auto &record : records.last(count) | std::views::reverse) {
    auto line = row(record);
    if (line.size() > 65536 - bytes)
      break;
    bytes += static_cast<unsigned>(line.size());
    rows.push_back(std::move(line));
  }
  std::string result;
  result.reserve(bytes);
  for (const auto &line : rows | std::views::reverse)
    result += line;
  return result;
}

std::string profile_dump(const n64::ProfileSnapshot &profile) {
  std::ostringstream out;
  constexpr std::array<std::string_view, 8> cpu{
      "CPU Count ticks",          "CPU exception Count ticks",    "Instruction cache hits",
      "Instruction cache misses", "Instruction cache writebacks", "Data cache hits",
      "Data cache misses",        "Data cache writebacks"};
  for (unsigned n = 0; n < cpu.size(); ++n)
    out << cpu[n] << ": " << profile.cpu[n] << "\r\n";
  out << "RSP pipeline clocks (3 per cycle): " << profile.rsp[0]
      << "\r\nRSP halted pipeline clocks: " << profile.rsp[1]
      << "\r\nCore RDRAM byte transfers (GPU raster accesses excluded):\r\n";
  for (unsigned n = 0; n < requestors.size(); ++n)
    out << "  " << requestors[n] << ": " << profile.traffic[n].reads << " read, "
        << profile.traffic[n].writes << " written\r\n";
  return out.str();
}

} // namespace cupid::desktop
