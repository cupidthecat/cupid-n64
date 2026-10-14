#include "core/debug/diagnostics.hpp"
#include "core/debug/inspection.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace cupid::n64 {

void Diagnostics::memory_log(DebugProcessor processor, std::uint64_t address, std::uint64_t length,
                             unsigned code) {
  if (code > 1)
    return;
  if (memory_fence_)
    memory_fence_();
  const auto count = code ? length : 1024ull * 1024;
  std::string message;
  for (std::uint64_t n = 0; (!code && processor == DebugProcessor::Cpu) || n < count; ++n) {
    const auto byte = Inspection::data_byte(*console_, processor, address + n, false).value_or(0);
    if (!code && !byte)
      break;
    message += static_cast<char>(byte);
  }
  text(processor, std::move(message));
}

void Diagnostics::memory_dump(DebugProcessor processor, std::uint64_t address,
                              std::uint64_t length) {
  if (memory_fence_)
    memory_fence_();
  const auto dump = [&]<typename Address>(Address start, Address count) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    for (Address offset = 0; offset < count; offset += 16) {
      const auto bytes = static_cast<unsigned>(std::min(count - offset, Address(16)));
      output << std::hex << std::setfill('0') << std::setw(sizeof(Address) * 2)
             << Address(start + offset) << ' ' << std::setw(4) << offset << ": ";
      std::string characters(16, ' ');
      for (unsigned n = 0; n < 16; ++n) {
        if (n >= bytes) {
          output << "   ";
        } else {
          const auto byte =
              Inspection::data_byte(*console_, processor, Address(start + offset + n), false)
                  .value_or(0);
          output << std::setw(2) << unsigned(byte) << ' ';
          characters[n] = byte >= 32 && byte <= 126 ? static_cast<char>(byte) : '.';
        }
        if (n == 7)
          output << ' ';
      }
      output << " |" << characters << "|\n";
    }
    text(processor, output.str());
  };
  if (processor == DebugProcessor::Rsp)
    dump(static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(length));
  else
    dump(address, length);
}

} // namespace cupid::n64
