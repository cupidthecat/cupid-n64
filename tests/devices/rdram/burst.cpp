#include "../fixture.hpp"

namespace test {
using namespace cupid::n64;

void rdram_burst_coverage_tests() {
  MemoryFixture fixture;
  fixture.initialize();
  auto tracker = fixture.ram.instruction_tracker();
  constexpr std::uint32_t physical = 0x1040;
  tracker->watch(physical, 64);
  std::vector<std::uint8_t> shared(fixture.ram.size() / 2);
  std::array<std::uint32_t, 8> values;
  alignas(16) const std::array<std::uint32_t, 9> unaligned{
      0, 0x00010000, 0x00000001, 0x00010001, 0, 0xabcdef90, 0x12345679, 0x11102220, 0xffff0001};
  for (bool bound : {false, true}) {
    equal(fixture.ram.bind_hidden(bound ? std::span(shared) : std::span<std::uint8_t>{}), true);
    for (unsigned count : {4u, 8u}) {
      const auto patterns = 1u << (count * 2);
      for (unsigned pattern = 0; pattern < patterns; ++pattern) {
        for (unsigned word = 0; word < count; ++word)
          values[word] = (0xa55a5aa4u & ~0x10001u) | (((pattern >> (word * 2)) & 1) << 16) |
                         ((pattern >> (word * 2 + 1)) & 1);
        auto hidden = fixture.ram.hidden();
        std::fill(hidden.begin() + physical / 2 - 1, hidden.begin() + physical / 2 + count * 2 + 1,
                  0xa5);
        const auto before = tracker->generation(physical);
        fixture.ram.write_burst(physical, std::span(values).first(count),
                                MemoryRequestor::DataCache);
        equal(tracker->generation(physical) - before, count);
        equal(hidden[physical / 2 - 1], 0xa5);
        equal(hidden[physical / 2 + count * 2], 0xa5);
        for (unsigned word = 0; word < count; ++word) {
          equal(fixture.ram.read(physical + word * 4, 4, MemoryRequestor::DataCache), values[word]);
          const auto offset = physical / 2 + word * 2;
          equal(hidden[offset], ((pattern >> (word * 2)) & 1) * 3);
          equal(hidden[offset + 1], ((pattern >> (word * 2 + 1)) & 1) * 3);
        }
      }
    }
    for (unsigned offset : {0u, 4u, 8u, 12u}) {
      for (unsigned count : {1u, 2u, 3u, 4u, 5u, 8u}) {
        const auto address = physical + offset;
        const auto words = std::span(unaligned).subspan(1, count);
        auto hidden = fixture.ram.hidden();
        std::fill(hidden.begin() + address / 2 - 1, hidden.begin() + address / 2 + count * 2 + 1,
                  0xa5);
        const auto before = tracker->generation(physical);
        fixture.ram.write_burst(address, words, MemoryRequestor::DataCache);
        equal(tracker->generation(physical) - before, count);
        equal(hidden[address / 2 - 1], 0xa5);
        equal(hidden[address / 2 + count * 2], 0xa5);
        for (unsigned word = 0; word < count; ++word) {
          equal(fixture.ram.read(address + word * 4, 4), words[word]);
          equal(hidden[address / 2 + word * 2], ((words[word] >> 16) & 1) * 3);
          equal(hidden[address / 2 + word * 2 + 1], (words[word] & 1) * 3);
        }
      }
    }
  }
  // A non-identity chip map still updates the physical coverage allocation.
  fixture.ram.write_word(0x03f00004, 0x28000000);
  equal(fixture.ram.identity(), false);
  values = {0x00010000, 0x00000001, 0x00010001, 0, 0, 0, 0, 0};
  fixture.ram.write_burst(0xa00000 + physical, std::span(values).first(4),
                          MemoryRequestor::DataCache);
  const std::array<std::uint8_t, 8> expected{3, 0, 0, 3, 3, 3, 0, 0};
  for (unsigned byte = 0; byte < expected.size(); ++byte)
    equal(fixture.ram.hidden()[physical / 2 + byte], expected[byte]);
}

} // namespace test
