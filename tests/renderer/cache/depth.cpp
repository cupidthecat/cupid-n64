#include "fixture.hpp"

namespace test {
using namespace renderer_cache;
namespace {

struct Depth {
  std::uint16_t primitive;
  std::uint16_t encoded;
};

struct Delta {
  std::uint16_t primitive;
  std::uint16_t encoded;
};

void depth_case(CacheFixture &fixture, Depth z, Delta delta, bool dirty, unsigned completion,
                unsigned operation) {
  const auto initial_failures = failures;
  auto &ram = fixture.console.ram();
  fixture.reset();
  for (unsigned n = 0; n < 4; ++n)
    ram.write(target + n * 4, 4, 0x12345678);
  equal(fixture.load(cached), 0x12345678);
  if (dirty)
    fixture.store(cached, 0xabcdef90);

  const auto stale = dirty ? 0xabcdef90u : 0x12345678u;
  const auto sample = [&](std::uint32_t cached_word, std::uint32_t memory_word, unsigned hidden0,
                          unsigned hidden1, unsigned clocks) {
    equal(fixture.load(cached), extended(cached_word));
    equal(fixture.load(direct), extended(memory_word));
    equal(ram.read(target + 4, 4), 0x12345678);
    equal(ram.hidden()[target / 2] & 3, hidden0);
    equal(ram.hidden()[target / 2 + 1] & 3, hidden1);
    equal(fixture.cpu.state().clocks, clocks);
  };
  sample(stale, 0x12345678, 0, 0, dirty ? 92 : 88);

  if (completion >= 2) {
    fixture.submit(surface(0xf801f801), true);
    fixture.begin_frame();
  }
  fixture.submit({{0x3f10003f, 0x180000},
                  {0x3e000000, target},
                  {0x2d000000, 0x00100100},
                  {0x2f0000f0, 0x24},
                  {0x2e000000, (std::uint32_t(z.primitive) << 16) | delta.primitive},
                  {0x3c000000, 0},
                  {0x36008004, 0}},
                 completion == 0 || completion == 3);
  if (completion >= 2)
    equal(fixture.read_frame(), red_frame);
  if (completion == 1 || completion == 2) {
    fixture.begin_frame();
    fixture.read_frame();
  }
  const auto half = std::uint32_t(z.encoded | (delta.encoded >> 2));
  const auto word = (half << 16) | half;
  sample(stale, word, delta.encoded & 3, delta.encoded & 3, dirty ? 98 : 94);

  fixture.maintain(operation, cached);
  const bool wrote = dirty && operation != 17;
  const auto memory_word = wrote ? 0xabcdef90u : word;
  const auto cached_word = !dirty && operation == 25 ? stale : memory_word;
  const unsigned clocks = !dirty ? (operation == 25 ? 102 : 180)
                                 : (operation == 17   ? 184
                                    : operation == 25 ? 186
                                                      : 264);
  sample(cached_word, memory_word, wrote ? 3 : delta.encoded & 3, wrote ? 0 : delta.encoded & 3,
         clocks);
  if (failures != initial_failures)
    std::cerr << "depth=" << z.primitive << " delta=" << delta.primitive << " dirty=" << dirty
              << " completion=" << completion << " operation=" << operation << '\n';
}

} // namespace

void gpu_depth_cache_tests(Console &console, HardwareRenderer &renderer) {
  CacheFixture fixture{console, renderer};
  constexpr Depth depths[] = {{0, 0},           {1, 0},           {0x1234, 0x0918},
                              {0x4000, 0x2000}, {0x7fff, 0xffe0}, {0xffff, 0xffe0}};
  constexpr Delta deltas[] = {{0, 0}, {1, 0},  {2, 1},   {4, 2},
                              {8, 3}, {16, 4}, {256, 8}, {0xffff, 15}};
  for (const auto z : depths)
    for (const auto delta : deltas)
      for (bool dirty : {false, true})
        for (unsigned completion : {0u, 1u, 2u, 3u})
          for (unsigned operation : {1u, 17u, 21u, 25u})
            depth_case(fixture, z, delta, dirty, completion, operation);
}

} // namespace test
