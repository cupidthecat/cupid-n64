#include "../../rsp/fixture.hpp"
#include "core/debug/disassembly.hpp"
#include "fixture.hpp"
#include <array>

namespace tracing_test {

void instruction_tests() {
  constexpr auto pc = 0xffffffff80001000ull;
  const auto decode = [&](std::uint32_t word, std::string_view expected) {
    const auto result = disassemble(DebugProcessor::Cpu, pc, word);
    if (result != expected)
      std::cerr << result << " != " << expected << '\n';
    equal(result == expected, true);
  };
  decode(0, "nop");
  decode(test::i(9, 1, 2, 0xffff), "addiu $r2, $r1, -1");
  decode(test::i(4, 1, 2, 0xffff), "beq $r1, $r2, 0xFFFFFFFF80001000");
  decode(test::i(21, 1, 2, 2), "bnel $r1, $r2, 0xFFFFFFFF8000100C");
  decode(test::i(1, 1, 18, 2), "bltzall $r1, 0xFFFFFFFF8000100C");
  decode(0x08000800, "j 0xFFFFFFFF80002000");
  decode(test::r(9, 1, 0, 31), "jalr $r31, $r1");
  decode(test::r(60, 0, 2, 3, 7), "dsll32 $r3, $r2, 7");
  decode(test::r(48, 1, 2, 0), "tge $r1, $r2");
  decode(test::co(2), "tlbwi");
  decode(test::co(24), "eret");
  decode(test::c(0, 2, 12), "mfc0 $r2, $c12");
  decode((17u << 26) | (16u << 21) | (2u << 16) | (3u << 11) | (4u << 6), "add.s $f4, $f3, $f2");
  decode((17u << 26) | (17u << 21) | (3u << 11) | (4u << 6) | 13, "trunc.w.d $f4, $f3");
  decode((17u << 26) | (21u << 21) | (3u << 11) | (4u << 6) | 32, "cvt.s.l $f4, $f3");
  decode((17u << 26) | (16u << 21) | (2u << 16) | (3u << 11) | 50, "c.eq.s $f3, $f2");
  decode(test::i(49, 1, 2, 0xfffc), "lwc1 $f2, -4($r1)");
  decode(test::i(53, 1, 2, 0xfffc), "ldc1 $f2, -4($r1)");
  decode(test::i(61, 1, 2, 0xfffc), "sdc1 $f2, -4($r1)");
  decode(test::i(54, 1, 2, 0xfffc), "ldc2 $c2, -4($r1)");
  decode(test::i(62, 1, 2, 0xfffc), "sdc2 $c2, -4($r1)");
  decode(test::i(63, 1, 2, 0xfffc), "sd $r2, -4($r1)");
  decode(0x7c000000, ".word 0x7C000000");
  const auto signal = [&](std::uint32_t word, std::string_view expected) {
    const auto result = disassemble(DebugProcessor::Rsp, 0xffc, word);
    if (result != expected)
      std::cerr << result << " != " << expected << '\n';
    equal(result == expected, true);
  };
  signal(test::i(4, 1, 2, 2), "beq $r1, $r2, 0x008");
  signal(0x08000800, "j 0x000");
  signal(test::c(0, 2, 7), "mfc0 $r2, SP_SEMAPHORE");
  signal(test::vector(16, 1, 2, 3, 8), "vadd $v1, $v2, $v3[8]");
  signal(test::vector(48, 1, 2, 3, 8), "vrcp $v1[2], $v3[8]");
  signal(test::vector(29, 1, 2, 3, 8), "vsar $v1, 8");
  signal(test::vector_memory(false, 4, 2, 1, 7, 127), "lqv $v2[7], -16($r1)");
  signal(test::vector_memory(true, 6, 2, 1, 0, 127), "spv $v2[0], -8($r1)");
  signal(test::vector_memory(false, 10, 2, 1, 0, 0), ".word 0xC8225000");
  for (unsigned n = 0; n < 32; ++n) {
    const auto word = test::i(9, n, 31 - n, 0xffff);
    const auto name = "addiu $r" + std::to_string(31 - n) + ", $r" + std::to_string(n) + ", -1";
    equal(disassemble(DebugProcessor::Cpu, pc, word) == name, true);
    equal(disassemble(DebugProcessor::Rsp, pc, word) == name, true);
  }
  equal(cupid::desktop::instruction_address(DebugProcessor::Cpu, "80001000"), pc);
  equal(cupid::desktop::instruction_address(DebugProcessor::Cpu, "0000000080001000"), 0x80001000);
  equal(cupid::desktop::instruction_address(DebugProcessor::Rsp, "ffc"), 0xffc);
  equal(cupid::desktop::instruction_row(DebugProcessor::Cpu, pc, {}).find("Unavailable") !=
            std::string::npos,
        true);
  auto console = machine();
  const std::array words{test::i(9, 0, 1, 7), test::i(9, 0, 2, 8), std::uint32_t(12)};
  code(*console, words);
  console->signal().write_local(0x1ffc, 4, test::vector(55, 0, 0, 0));
  const auto before = CoreState::capture(*console);
  for (unsigned n = 0; n < 16; ++n) {
    equal(Inspection::instruction(*console, DebugProcessor::Cpu, pc).value_or(0), words[0]);
    equal(Inspection::instruction(*console, DebugProcessor::Cpu, 0xffffffffa0001004).value_or(0),
          words[1]);
    equal(Inspection::instruction(*console, DebugProcessor::Rsp, 0xffc).value_or(0),
          test::vector(55, 0, 0, 0));
    equal(Inspection::instruction(*console, DebugProcessor::Cpu, 0x20000000).has_value(), false);
    equal(Inspection::instruction(*console, DebugProcessor::Cpu, 0xffffffffa404001c).has_value(),
          false);
    equal(Inspection::instruction(*console, DebugProcessor::Cpu, pc + 1).has_value(), false);
    equal(Inspection::instruction(*console, DebugProcessor::Rsp, 0x1000).has_value(), false);
  }
  equal(CoreState::capture(*console) == before, true);
  console->cpu().step();
  raw_word(*console, 0x1000, 0x12345678);
  const auto cached = CoreState::capture(*console);
  equal(Inspection::instruction(*console, DebugProcessor::Cpu, pc).value_or(0), words[0]);
  equal(Inspection::instruction(*console, DebugProcessor::Cpu, 0xffffffffa0001000).value_or(0),
        0x12345678);
  equal(CoreState::capture(*console) == cached, true);
  console->cpu().write_control(EntryHi, 0x20000000);
  console->cpu().write_control(EntryLo0, 0x5f);
  console->cpu().write_control(EntryLo1, 0x9f);
  console->cpu().write_control(Index, 1);
  console->cpu().execute(test::co(2));
  const auto mapped = CoreState::capture(*console);
  equal(Inspection::instruction(*console, DebugProcessor::Cpu, 0x20000000).value_or(0), 0x12345678);
  equal(CoreState::capture(*console) == mapped, true);
  console->cpu().write_control(Config, 0x70066460);
  equal(Inspection::instruction(*console, DebugProcessor::Cpu, 0xffffffffa0001000).value_or(0),
        words[1]);
  auto swapped = machine();
  raw_word(*swapped, 0, 0x24010011);
  raw_word(*swapped, 0x200000, 0x24010022);
  swapped->ram().write_word(0x03f00004, 8u << 26);
  swapped->ram().write_word(0x03f00804, 0);
  swapped->ram().write_word(0x03f02004, 2u << 26);
  equal(swapped->ram().identity(), false);
  const auto swapped_before = CoreState::capture(*swapped);
  equal(Inspection::instruction(*swapped, DebugProcessor::Cpu, 0xffffffffa0000000).value_or(0),
        0x24010022);
  equal(Inspection::instruction(*swapped, DebugProcessor::Cpu, 0xffffffffa0200000).value_or(0),
        0x24010011);
  equal(CoreState::capture(*swapped) == swapped_before, true);
  swapped->cpu().set_pc(0xffffffffa0000000);
  swapped->cpu().step();
  equal(swapped->cpu().state().gpr[1], 0x22);
  swapped->ram().write_word(0x03f0000c, 0);
  const auto disabled = CoreState::capture(*swapped);
  equal(Inspection::instruction(*swapped, DebugProcessor::Cpu, 0xffffffffa0000000).has_value(),
        false);
  equal(CoreState::capture(*swapped) == disabled, true);
  auto extended = machine();
  raw_word(*extended, 0x2000, 0x24010033);
  extended->cpu().write_control(Status, 0x300000e0);
  extended->cpu().write_control(EntryHi, 0x80000000);
  extended->cpu().write_control(EntryLo0, 0x5f);
  extended->cpu().write_control(EntryLo1, 0x9f);
  extended->cpu().write_control(Index, 2);
  extended->cpu().execute(test::co(2));
  const auto extended_before = CoreState::capture(*extended);
  equal(Inspection::instruction(
            *extended, DebugProcessor::Cpu,
            cupid::desktop::instruction_address(DebugProcessor::Cpu, "0000000080001000"))
            .value_or(0),
        0x24010033);
  equal(CoreState::capture(*extended) == extended_before, true);
}

} // namespace tracing_test
