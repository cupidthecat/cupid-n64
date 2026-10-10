#pragma once
#include <array>
#include <cstdint>
namespace test::flash_boundary {
constexpr unsigned Bytes = 131072;
constexpr std::uint8_t pattern(unsigned n) {
  return static_cast<std::uint8_t>(n * 53 + (n >> 7) * 17 + 0x29);
}
constexpr unsigned duration(unsigned model, unsigned operation) {
  return operation == 0 ? (model == 6 ? 56250 : 656250)
         : model == 6   ? (operation == 1 ? 52500000 : 56250000)
                        : 15937500;
}
template <class Probe> void command(Probe &p, std::uint32_t value) {
  p.select(0x08010000);
  p.write_half(static_cast<std::uint16_t>(value >> 16));
  p.write_half(static_cast<std::uint16_t>(value));
}
template <class Probe> void observe(Probe &p) {
  p.word(static_cast<std::uint32_t>(p.deadline()));
  p.word(p.fired.size());
  for (unsigned n = 0; n < 16; ++n)
    p.word(n < p.fired.size() ? p.fired[n] : 0);
  for (unsigned address : {0x08000000u, 0x0801fffeu, 0x08020000u, 0x08000000u}) {
    p.word(p.select(address));
    for (unsigned n = 0; n < 4; ++n)
      p.word(p.read_half());
  }
}
template <class Probe> void snapshot(Probe &p) {
  p.word(Bytes);
  for (unsigned n = 0; n < Bytes; n += 8) {
    std::uint64_t value = 0;
    for (unsigned b = 0; b < 8; ++b)
      value = (value << 8) | p.byte(n + b);
    p.word(value);
  }
}
template <class Probe> void scenarios(Probe &p) {
  for (unsigned model = 0; model < 7; ++model)
    for (unsigned operation = 0; operation < 3; ++operation)
      for (unsigned page : {0u, 127u, 128u, 1023u})
        for (unsigned epoch : {0u, 0xfffffff0u})
          for (unsigned boundary = 0; boundary < 5; ++boundary)
            for (bool warm : {false, true})
              for (unsigned order = 0; order < 3; ++order) {
                p.reset(model, epoch);

                test::flash_boundary::observe(p);
                command(p, 0xe1000000);
                test::flash_boundary::observe(p);
                command(p, 0xb4000000);
                p.select(0x08000000);
                for (unsigned n = 0; n < 64; ++n)
                  p.write_half(static_cast<std::uint16_t>(0x5a00 | (n * 7 & 255)));
                p.select(0x08000000);
                p.write_half(0xf0ff);
                test::flash_boundary::observe(p);
                const auto clocks = duration(model, operation);
                if (order == 1)
                  p.markers(clocks);
                command(p, operation == 0   ? 0xa5000000 | page
                           : operation == 1 ? 0x4b000000 | page
                                            : 0x3c000000);
                if (operation != 0)
                  command(p, 0x78000000);
                if (order == 2)
                  p.markers(clocks);
                test::flash_boundary::observe(p);
                snapshot(p);
                command(p, 0xf0000000);
                command(p, 0xa5000000 | ((page + 1) & 1023));
                test::flash_boundary::observe(p);
                const std::array<unsigned, 5> steps{0, 1, clocks - 1, clocks, clocks + 1};
                p.advance(steps[boundary]);
                command(p, 0xd2000000);
                test::flash_boundary::observe(p);
                p.power(warm);
                test::flash_boundary::observe(p);
                p.advance(clocks + 1);
                test::flash_boundary::observe(p);
                command(p, 0xe1000000);
                test::flash_boundary::observe(p);
                snapshot(p);
                p.finish();
              }
}
} // namespace test::flash_boundary
