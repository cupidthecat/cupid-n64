#pragma once

#include "../../support/test.hpp"
#include "core/system/console.hpp"
#include "renderer/vulkan/renderer.hpp"
#include "video.hpp"
#include <memory>
#include <span>

namespace test::renderer_replay {
using namespace cupid::n64;

inline std::uint64_t fingerprint(std::span<const std::uint8_t> bytes) {
  std::uint64_t value = 14695981039346656037ull;
  for (auto byte : bytes)
    value = (value ^ byte) * 1099511628211ull;
  return value;
}

struct GpuFixture {
  Console console{ConsoleConfig{.random_seed = 0}};
  std::unique_ptr<HardwareRenderer> renderer;

  GpuFixture() {
    map_memory();
    connect();
  }

  void map_memory() {
    console.write(0x04700008, 4, 0);
    console.write(0x0470000c, 4, 0x14);
    console.write(0x04300000, 4, 0x10f);
    console.write(0x03f80008, 4, 0x00080008);
    for (unsigned chip = 0; chip < 4; ++chip) {
      console.write(0x03f0000c, 4, 0x02000000);
      console.write(0x03f00004, 4, (chip + 4) * 2 << 26);
    }
    for (unsigned chip = 0; chip < 4; ++chip)
      console.write(0x03f00004 + (chip + 4) * 0x800, 4, chip * 2 << 26);
  }

  void connect() {
    renderer = std::make_unique<HardwareRenderer>(console.ram());
    console.video().connect_registers(
        [this](unsigned index, std::uint32_t value) { renderer->write_video(index, value); });
    console.display().connect(
        [this](std::span<const std::uint32_t> words) { renderer->submit(words); },
        [this] { renderer->synchronize(); },
        [this] {
          if (renderer->crashed())
            console.display().crash();
        });
  }

  void put(std::uint32_t address, std::uint32_t value) {
    console.ram().write(address, 4, value);
  }

  void hidden(unsigned address, unsigned value) {
    console.ram().hidden()[address] = static_cast<std::uint8_t>(value);
  }

  void program(const ViRegisters &registers) {
    for (unsigned n = 0; n < registers.size(); ++n)
      console.video().write_word(n * 4, registers[n]);
  }

  void reset_commands() {
    console.cpu().power();
    console.display().power();
    console.interrupts().write_word(0, 0x800);
  }

  void power(bool warm) {
    renderer.reset();
    console.power(warm);
    map_memory();
    connect();
  }

  void command_word(unsigned address, unsigned value, bool xbus) {
    if (xbus)
      console.signal().write_local(address & 0xfff, 4, value);
    else
      put(address, value);
  }

  void display_write(unsigned index, unsigned value) {
    console.display().write_word(index * 4, value);
  }

  unsigned irq() {
    return console.interrupts().read_word(8);
  }

  std::array<std::uint32_t, 8> display_registers() {
    std::array<std::uint32_t, 8> registers{};
    for (unsigned n = 0; n < registers.size(); ++n)
      registers[n] = console.display().read_word(n * 4);
    return registers;
  }

  bool crashed() {
    return renderer->crashed() || console.display().command().crashed;
  }
};

} // namespace test::renderer_replay
