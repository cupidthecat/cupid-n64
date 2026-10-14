#pragma once

#include "homebrew.hpp"
#include <algorithm>

namespace debug_test {

inline std::string memory_output(Console &console, DebugProcessor processor, unsigned function,
                                 std::uint64_t address, std::uint64_t length, unsigned option = 0) {
  console.diagnostics().clear_log();
  if (processor == DebugProcessor::Cpu) {
    console.cpu().state().gpr[1] = address;
    console.cpu().state().gpr[2] = length;
    console.cpu().execute(emux(function, 1, 2, option));
  } else {
    console.signal().state().gpr[1] = static_cast<std::uint32_t>(address);
    console.signal().state().gpr[2] = static_cast<std::uint32_t>(length);
    console.signal().execute(emux(function, 1, 2, option));
  }
  std::string text;
  for (const auto &record : console.diagnostics().records())
    if (record.kind == DiagnosticKind::HomebrewLog)
      text += record.text;
  return text;
}

inline void homebrew_output_probes() {
  auto console = machine(130);
  console->diagnostics().set_homebrew(true);
  const auto write = [&](std::uint32_t address, std::span<const std::uint8_t> bytes) {
    for (unsigned offset = 0; offset < bytes.size(); offset += Inspection::maximum_transfer)
      Inspection::write(*console, MemoryRegionId::Rdram, address + offset,
                        bytes.subspan(offset, std::min<std::size_t>(bytes.size() - offset,
                                                                    Inspection::maximum_transfer)));
  };
  std::vector<std::uint8_t> bytes(5007, 'a');
  bytes[17] = 0;
  write(0x2000, bytes);
  auto output =
      memory_output(*console, DebugProcessor::Cpu, 0x25, 0xffffffffa0002000, bytes.size(), 1);
  equal(output == std::string(bytes.begin(), bytes.end()), true);
  bytes[17] = 'a';
  write(0x2000, bytes);
  output = memory_output(*console, DebugProcessor::Cpu, 0x25, 0xffffffffa0002000, 0);
  equal(output == std::string(bytes.begin(), bytes.end()), true);
  output = memory_output(*console, DebugProcessor::Cpu, 0x27, 0xffffffffa0002000, bytes.size());
  equal(std::count(output.begin(), output.end(), '\n'), 313);
  equal(output.starts_with("ffffffffa0002000 0000: 61 61 61 61 61 61 61 61  "), true);
  equal(output.find("ffffffffa0003380 1380: ") != std::string::npos, true);
  equal(output.ends_with(" |aaaaaaaaaaaaaaa |\n"), true);
  output = memory_output(*console, DebugProcessor::Cpu, 0x25, 0x12345000, 4, 1);
  equal(output == std::string(4, '\0'), true);

  bytes.assign(2 * 1024 * 1024 + 1, 'b');
  write(0x2000, bytes);
  output = memory_output(*console, DebugProcessor::Cpu, 0x25, 0xffffffffa0002000, 0);
  equal(output.size(), bytes.size());
  equal(std::all_of(output.begin(), output.end(), [](char ch) { return ch == 'b'; }), true);

  auto &rsp = console->signal();
  for (unsigned address = 0; address < 8192; ++address)
    rsp.write_local(address, 1, 32 + address % 95);
  std::string expected;
  for (unsigned n = 0; n < 5007; ++n)
    expected += static_cast<char>(32 + ((4091 + n) & 8191) % 95);
  output = memory_output(*console, DebugProcessor::Rsp, 0x25, 4091, 5007, 1);
  equal(output == expected, true);
  output = memory_output(*console, DebugProcessor::Rsp, 0x27, 0xfffffff8, 17);
  equal(output.starts_with("fffffff8 0000: "), true);
  equal(output.find("00000008 0010: ") != std::string::npos, true);
  equal(std::count(output.begin(), output.end(), '\n'), 2);
  output = memory_output(*console, DebugProcessor::Rsp, 0x27, 4091, 5007);
  equal(std::count(output.begin(), output.end(), '\n'), 313);
  equal(output.find("0000237b 1380: ") != std::string::npos, true);
  output = memory_output(*console, DebugProcessor::Rsp, 0x27, 0, 0);
  equal(output.empty(), true);
  equal(console->diagnostics().records().empty(), true);

  for (unsigned address = 0; address < 8192; address += 4)
    rsp.write_local(address, 4, 0x63636363);
  output = memory_output(*console, DebugProcessor::Rsp, 0x25, 0, 0);
  equal(output.size(), 1024 * 1024);
  equal(std::all_of(output.begin(), output.end(), [](char ch) { return ch == 'c'; }), true);
  rsp.write_local(5007, 1, 0);
  output = memory_output(*console, DebugProcessor::Rsp, 0x25, 0, 0);
  equal(output.size(), 5007);
  output = memory_output(*console, DebugProcessor::Rsp, 0x25, 0, 0, 1);
  equal(output.empty(), true);
  equal(console->diagnostics().records().empty(), true);
}

} // namespace debug_test
