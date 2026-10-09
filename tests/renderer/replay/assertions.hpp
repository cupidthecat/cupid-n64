#pragma once

#include "commands.hpp"
#include "fixture.hpp"
#include "memory_expected.hpp"

namespace test::renderer_replay {

struct CaptureContext {
  const RenderCase &input;
  unsigned id, stage, route;
  bool sync;

  void print() const {
    std::cerr << "RDP case " << id << ' ' << input.name << " stage " << stage << " route " << route
              << " sync " << sync << " format " << input.framebuffer << " opcode " << input.opcode
              << " texture " << input.texture << " filter " << input.filter << " cycle "
              << input.cycle << " load " << input.load << " combiner " << input.combiner
              << " layer " << input.layer << " modes 0x" << std::hex << input.modes << std::dec
              << '\n';
  }
};

inline bool compare_memory(GpuFixture &fixture, unsigned index, const CaptureContext &context) {
  const auto &range = expected::memories[index];
  const unsigned color_bytes = context.input.framebuffer == 2 ? 512 : 1024;
  const unsigned depth_end = color_bytes + 512, hidden_color_end = depth_end + color_bytes / 2;
  equal(range.bytes, hidden_color_end + 256);
  unsigned offset = 0;
  for (unsigned run = 0; run < range.count; ++run) {
    const auto &encoded = expected::memory_runs[range.first + run];
    for (unsigned word = 0; word < encoded.count; ++word)
      for (unsigned byte = 0; byte < 4; ++byte, ++offset) {
        unsigned actual = 0, address = 0;
        const char *plane = nullptr;
        if (offset < color_bytes) {
          plane = "color";
          address = color_address + offset;
          actual = static_cast<unsigned>(fixture.console.ram().read(address, 1));
        } else if (offset < depth_end) {
          plane = "depth";
          address = depth_address + offset - color_bytes;
          actual = static_cast<unsigned>(fixture.console.ram().read(address, 1));
        } else if (offset < hidden_color_end) {
          plane = "color hidden";
          address = color_address / 2 + offset - depth_end;
          actual = fixture.console.ram().hidden()[address] & 3;
        } else {
          plane = "depth hidden";
          address = depth_address / 2 + offset - hidden_color_end;
          actual = fixture.console.ram().hidden()[address] & 3;
        }
        const auto wanted = (encoded.word >> (byte * 8)) & 255;
        equal(actual, wanted);
        if (actual != wanted) {
          context.print();
          std::cerr << "First " << plane << " difference at 0x" << std::hex << address << std::dec
                    << '\n';
          return false;
        }
      }
  }
  equal(offset, range.bytes);
  return true;
}

inline bool capture(GpuFixture &fixture, const CaptureContext &context, unsigned memory,
                    std::uint64_t pixels, const std::array<std::uint32_t, 8> &dpc, unsigned irq,
                    bool crashed) {
  const auto before = failures;
  const auto frame = fixture.renderer->frame(false);
  equal(frame.width, 640);
  equal(frame.height, 240);
  equal(frame.rgba.size(), 614400);
  equal(fingerprint(frame.rgba), pixels);
  const auto registers = fixture.display_registers();
  for (unsigned n = 0; n < registers.size(); ++n)
    equal(registers[n], dpc[n]);
  equal(fixture.irq(), irq);
  equal(fixture.crashed(), crashed);
  if (failures != before) {
    context.print();
    return false;
  }
  return compare_memory(fixture, memory, context);
}

inline void upload(GpuFixture &fixture, std::span<const RdpPacket> packets, unsigned base,
                   bool xbus) {
  for (unsigned n = 0; n < packets.size(); ++n)
    for (unsigned half = 0; half < 2; ++half)
      fixture.command_word(base + n * 8 + half * 4, packets[n][half], xbus);
}

} // namespace test::renderer_replay
