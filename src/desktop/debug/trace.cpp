#include "core/debug/disassembly.hpp"
#include "desktop/debug/format.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <iomanip>
#include <sstream>

namespace cupid::desktop {

std::string instruction_row(n64::DebugProcessor processor, std::uint64_t pc,
                            std::optional<std::uint32_t> instruction) {
  std::ostringstream out;
  out << std::hex << std::uppercase << std::setfill('0')
      << std::setw(processor == n64::DebugProcessor::Rsp ? 3 : 16) << pc << "  ";
  if (instruction)
    out << std::setw(8) << *instruction << "  " << n64::disassemble(processor, pc, *instruction);
  else
    out << "--------  Unavailable";
  return out.str();
}

std::string trace_dump(std::span<const n64::TraceRecord> records, unsigned limit) {
  std::ostringstream out;
  const auto count = std::min<std::size_t>({records.size(), limit, 256});
  for (const auto &record : records.last(count)) {
    out << std::dec << record.sequence << "  ";
    using n64::TraceKind;
    const bool rsp = record.kind == TraceKind::RspInstruction;
    if (rsp)
      out << "RSP offset " << std::bit_cast<std::int64_t>(record.clock);
    else
      out << "CPU clock " << record.clock;
    out << "  ";
    if (record.kind == TraceKind::CpuInstruction || rsp)
      out << instruction_row(rsp ? n64::DebugProcessor::Rsp : n64::DebugProcessor::Cpu, record.pc,
                             record.instruction);
    else {
      out << std::hex << std::uppercase << std::setfill('0') << std::setw(16) << record.pc;
      if (record.kind == TraceKind::Exception)
        out << "  exception " << std::dec << record.code << " coprocessor " << record.index;
      else if (record.kind == TraceKind::Interrupt)
        out << "  interrupt mask 0x" << record.code;
      else if (record.kind == TraceKind::Nmi)
        out << "  NMI";
      else if (record.kind == TraceKind::Tlb) {
        constexpr std::array<std::string_view, 6> names{"load",    "store", "write",
                                                        "invalid", "miss",  "modification"};
        const auto event = static_cast<unsigned>(record.tlb);
        out << "  TLB " << (event < names.size() ? names[event] : "unknown") << " address 0x"
            << record.address;
        if (record.tlb == n64::TlbEvent::Write)
          out << " index " << std::dec << record.index << std::hex << " mask 0x" << record.mask
              << " lo0 0x" << record.lo0 << " lo1 0x" << record.lo1;
        else if (record.tlb == n64::TlbEvent::Load || record.tlb == n64::TlbEvent::Store)
          out << " physical 0x" << record.value;
      }
    }
    if (record.delay_slot)
      out << "  [delay slot]";
    out << "\r\n";
  }
  return out.str();
}

} // namespace cupid::desktop
