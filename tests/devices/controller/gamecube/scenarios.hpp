#pragma once

#include <array>
#include <cstdint>

namespace test::gamecube {

struct Input {
  std::uint16_t buttons = 0;
  std::int16_t x = 0, y = 0, cx = 0, cy = 0, l = 0, r = 0;
};

template <class Probe>
void command(Probe &p, unsigned opcode, unsigned send, unsigned receive, unsigned mode = 3,
             unsigned motor = 0) {
  const std::array<std::uint8_t, 6> request{static_cast<std::uint8_t>(opcode),
                                            static_cast<std::uint8_t>(mode),
                                            static_cast<std::uint8_t>(motor),
                                            0x55,
                                            0xa5,
                                            0x7e};
  p.command(request, send, receive);
}

template <class Probe> void scenarios(Probe &p) {
  p.input({0x5f1b, 17000, -25000, -29000, 9000, 12000, 26000});
  for (unsigned opcode = 0; opcode < 256; ++opcode) {
    p.reset();
    command(p, 0x40, 3, 8, 3, 1);
    for (unsigned send = 1; send <= 6; ++send)
      for (unsigned receive = 0; receive <= 16; ++receive)
        command(p, opcode, send, receive, 2, 0);
    p.finish();
  }

  for (unsigned mode = 0; mode < 256; ++mode) {
    p.reset();
    for (unsigned motor = 0; motor < 256; ++motor) {
      command(p, 0x40, 3, 8, mode, motor);
      command(p, motor & 2 ? 0xff : 0, 1, 3);
    }
    p.finish();
  }

  for (unsigned origin : {0x41u, 0x42u})
    for (unsigned receive = 0; receive <= 16; ++receive) {
      p.reset();
      command(p, 0x40, 3, 8, 3, 1);
      command(p, origin, 1, receive);
      command(p, 0x43, 1, 10);
      command(p, 0xff, 1, 3);
      p.reset();
      command(p, 0x43, 1, 10);
      command(p, 0, 1, 3);
    }
  p.finish();

  p.reset();
  command(p, 0x41, 1, 10);
  constexpr std::array<std::int16_t, 25> ys{
      -32768, -32767, -30000, -24576, -23170, -23169, -16384, -8192, -2580, -2579, -2278, -2277, -1,
      0,      1,      2277,   2278,   2579,   2580,   8192,   16384, 23169, 23170, 30000, 32767};
  for (auto y : ys) {
    for (int x = -32768; x <= 32767; ++x) {
      p.input({0, static_cast<std::int16_t>(x), y, y, static_cast<std::int16_t>(x)});
      command(p, 0x43, 1, 10);
    }
    p.finish();
  }

  for (unsigned x = 0; x < 256; ++x)
    for (unsigned y = 0; y < 256; ++y) {
      const auto ax = static_cast<std::int16_t>(int(x * 257) - 32768);
      const auto ay = static_cast<std::int16_t>(int(y * 257) - 32768);
      p.input({0, ax, ay, ay, ax});
      command(p, 0x43, 1, 10);
    }
  p.finish();

  std::uint32_t seed = 0x6c636e31;
  auto next = [&] { return seed = seed * 1664525 + 1013904223; };
  for (unsigned block = 0; block < 4; ++block) {
    for (unsigned n = 0; n < 65536; ++n) {
      Input input;
      input.buttons = static_cast<std::uint16_t>(next() >> 16);
      input.x = static_cast<std::int16_t>(next() >> 16);
      input.y = static_cast<std::int16_t>(next() >> 16);
      input.cx = static_cast<std::int16_t>(next() >> 16);
      input.cy = static_cast<std::int16_t>(next() >> 16);
      input.l = static_cast<std::int16_t>(next() >> 16);
      input.r = static_cast<std::int16_t>(next() >> 16);
      p.input(input);
      const auto mode = next() >> 24;
      const auto motor = next() >> 24;
      command(p, 0x40, 3, 8, mode, motor);
    }
    p.finish();
  }

  for (unsigned buttons = 0; buttons < 65536; ++buttons) {
    p.input({static_cast<std::uint16_t>(buttons), 16384, -16384, -16384, 16384, 12000, 27000});
    command(p, 0x43, 1, 10);
  }
  p.finish();

  for (unsigned digital : {0u, 0x6000u}) {
    for (int trigger = -32768; trigger <= 32767; ++trigger) {
      p.input({static_cast<std::uint16_t>(digital), 0, 0, 0, 0, static_cast<std::int16_t>(trigger),
               static_cast<std::int16_t>(32767 - trigger)});
      command(p, 0x43, 1, 10);
    }
    p.finish();
  }
}

} // namespace test::gamecube
