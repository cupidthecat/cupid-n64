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
  const auto count = code ? std::min(length, std::uint64_t(maximum_text)) : maximum_text;
  std::string message;
  message.reserve(static_cast<std::size_t>(count));
  for (std::uint64_t n = 0; n < count; ++n) {
    const auto byte = Inspection::data_byte(*console_, processor, address + n);
    if (!byte) {
      message += "<unmapped>";
      break;
    }
    if (!code && !*byte)
      break;
    message += static_cast<char>(*byte);
  }
  text(processor, std::move(message));
}

void Diagnostics::memory_dump(DebugProcessor processor, std::uint64_t address,
                              std::uint64_t length) {
  if (memory_fence_)
    memory_fence_();
  std::ostringstream output;
  output.imbue(std::locale::classic());
  const auto count = std::min(length, std::uint64_t(maximum_text));
  for (std::uint64_t offset = 0; offset < count; offset += 16) {
    const auto bytes = static_cast<unsigned>(std::min(count - offset, std::uint64_t(16)));
    output << std::hex << std::setfill('0') << std::setw(processor == DebugProcessor::Cpu ? 16 : 8)
           << address + offset << "  ";
    std::string characters;
    for (unsigned n = 0; n < 16; ++n) {
      if (n == 8)
        output << ' ';
      if (n >= bytes) {
        output << "   ";
        continue;
      }
      const auto byte = Inspection::data_byte(*console_, processor, address + offset + n);
      if (byte) {
        output << std::setw(2) << unsigned(*byte) << ' ';
        characters += *byte >= 32 && *byte <= 126 ? static_cast<char>(*byte) : '.';
      } else {
        output << "?? ";
        characters += '?';
      }
    }
    output << " |" << characters << "|\n";
    if (output.tellp() >= maximum_text)
      break;
  }
  text(processor, output.str());
}

} // namespace cupid::n64
