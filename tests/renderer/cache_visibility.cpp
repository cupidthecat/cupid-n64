#include "../support/test.hpp"
#include "core/system/console.hpp"
#include "renderer/vulkan/renderer.hpp"

namespace test {
using namespace cupid::n64;
namespace {

constexpr std::uint32_t target = 0x100000;
constexpr std::uint64_t cached = 0xffffffff80100000;
constexpr std::uint64_t uncached = 0xffffffffa0100000;

void execute(Cpu &cpu, bool native) {
  cpu.set_pc(cached);
  for (unsigned n = 0; n < 8 && cpu.state().pc != cached + 8; ++n)
    if (!native || !cpu.run_block(cpu.state().clocks))
      cpu.step();
  equal(cpu.state().pc, cached + 8);
  equal(cpu.read_control(Cause) & 0x7c, 0);
}

std::uint64_t load(Cpu &cpu, std::uint64_t address) {
  cpu.state().gpr[30] = address;
  cpu.execute(i(35, 30, 1, 0));
  return cpu.state().gpr[1];
}

void maintain(Cpu &cpu, unsigned operation) {
  cpu.state().gpr[30] = cached;
  cpu.execute(i(47, 30, operation, 0));
}

void fill(Console &console, HardwareRenderer &renderer, bool frame, std::uint32_t word,
          bool word32 = true) {
  const std::uint32_t packets[][2] = {{word32 ? 0x3f18003fu : 0x3f10003fu, target},
                                      {0x2d000000, 0x00100100},
                                      {0x2f300000, 0},
                                      {0x37000000, word},
                                      {word32 ? 0x36000000u : 0x36004000u, 0},
                                      {0x29000000, 0}};
  const unsigned count = frame ? 5 : 6;
  for (unsigned n = 0; n < count; ++n) {
    console.ram().write(0x2000 + n * 8, 4, packets[n][0]);
    console.ram().write(0x2004 + n * 8, 4, packets[n][1]);
  }
  console.display().write_word(0, 0x2000);
  console.display().write_word(4, 0x2000 + count * 8);
  if (frame) {
    renderer.begin_frame(false);
    const auto pixels = renderer.read_frame();
    equal(pixels.rgba.empty(), false);
  }
  equal(renderer.crashed(), false);
}

} // namespace

void gpu_cache_visibility_tests(Console &console, HardwareRenderer &renderer) {
  auto &cpu = console.cpu();
  for (bool native : {false, true}) {
    for (bool frame : {false, true}) {
      for (unsigned operation : {0u, 8u, 16u, 20u, 24u, 31u}) {
        cpu.power();
        cpu.write_control(Status, 0x30000000);
        cpu.write_control(Compare, 0xffffffff);
        cpu.state().gpr[5] = 0x30000000;
        console.display().power();
        console.ram().write(target, 4, i(9, 0, 4, 1));
        console.ram().write(target + 4, 4, c(4, 5, Status));
        execute(cpu, native);
        equal(cpu.state().gpr[4], 1);
        equal(load(cpu, cached), i(9, 0, 4, 1));
        const auto generation = console.instruction_tracker()->generation(target);
        fill(console, renderer, frame, i(9, 0, 4, 7));
        equal(console.instruction_tracker()->generation(target), generation);
        equal(load(cpu, uncached), i(9, 0, 4, 7));
        equal(load(cpu, cached), i(9, 0, 4, 1));
        execute(cpu, native);
        equal(cpu.state().gpr[4], 1);
        maintain(cpu, operation);
        const auto visible = operation == 24 ? 1u : 7u;
        for (unsigned pass = 0; pass < 3; ++pass) {
          execute(cpu, native);
          equal(cpu.state().gpr[4], native || operation == 31 ? 1 : visible);
        }
        equal(load(cpu, uncached), i(9, 0, 4, static_cast<std::uint16_t>(visible)));
        equal(load(cpu, cached), i(9, 0, 4, 1));
        maintain(cpu, 17);
        equal(load(cpu, cached), i(9, 0, 4, static_cast<std::uint16_t>(visible)));
        cpu.state().gpr[30] = frame ? cached : uncached;
        cpu.state().gpr[1] = i(9, 0, 4, 13);
        cpu.execute(i(43, 30, 1, 0));
        if (frame)
          maintain(cpu, 21);
        maintain(cpu, 16);
        execute(cpu, native);
        equal(cpu.state().gpr[4], 13);
        if (native)
          equal(console.instruction_tracker()->generation(target) != generation, true);
      }
    }
  }
  const auto extended = [](std::uint32_t word) {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(word)));
  };
  for (bool dirty : {false, true}) {
    for (bool frame : {false, true}) {
      for (unsigned operation : {1u, 17u, 21u, 25u}) {
        cpu.power();
        cpu.write_control(Status, 0x30000000);
        console.display().power();
        console.ram().write(target, 4, 0x12345678);
        equal(load(cpu, cached), 0x12345678);
        if (dirty) {
          cpu.state().gpr[30] = cached;
          cpu.state().gpr[1] = 0xabcdef90;
          cpu.execute(i(43, 30, 1, 0));
        }
        fill(console, renderer, frame, 0xf801f801, false);
        equal(load(cpu, cached), extended(dirty ? 0xabcdef90 : 0x12345678));
        equal(load(cpu, uncached), extended(0xf801f801));
        equal(console.ram().hidden()[target / 2] & 3, 3);
        equal(console.ram().hidden()[target / 2 + 1] & 3, 3);
        maintain(cpu, operation);
        const auto wrote = dirty && operation != 17;
        const auto word = wrote ? 0xabcdef90u : 0xf801f801u;
        equal(load(cpu, uncached), extended(word));
        equal(load(cpu, cached), extended(!dirty && operation == 25 ? 0x12345678 : word));
        equal(console.ram().hidden()[target / 2] & 3, 3);
        equal(console.ram().hidden()[target / 2 + 1] & 3, wrote ? 0 : 3);
      }
    }
  }
}

} // namespace test
