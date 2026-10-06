#include "core/devices/cic/cic.hpp"
#include "fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

template <std::size_t Size> void descramble(std::array<unsigned, Size> &data) {
  for (unsigned n = static_cast<unsigned>(Size) - 1; n; --n)
    data[n] = (data[n] - data[n - 1] - 1) & 15;
}

void boot(Cic &cic, bool pal, unsigned seed, std::uint64_t checksum) {
  equal(cic.read_nibble(), pal ? 5 : 1);
  std::array<unsigned, 6> seeds{};
  for (auto &value : seeds)
    value = cic.read_nibble();
  descramble(seeds);
  descramble(seeds);
  equal((seeds[2] << 4) | seeds[3], seed);
  equal((seeds[4] << 4) | seeds[5], seed);
  std::array<unsigned, 16> words{};
  for (auto &value : words)
    value = cic.read_nibble();
  for (unsigned n = 0; n < 4; ++n)
    descramble(words);
  std::uint64_t actual = 0;
  for (unsigned n = 4; n < words.size(); ++n)
    actual = (actual << 4) | words[n];
  equal(actual, checksum);
}

} // namespace

void cic_tests() {
  {
    Cic cic;
    boot(cic, false, 0x3f, 0xa536c0f1d859);
    cic.write_bit(true);
    cic.write_bit(false);
    equal(cic.read_nibble(), 10);
    equal(cic.read_nibble(), 10);
    for (unsigned n = 0; n < 30; ++n)
      cic.write_nibble(n & 15);
    equal(cic.read_bit(), false);
    for (unsigned n = 0; n < 30; ++n)
      equal(cic.read_nibble(), (n & 15) ^ 15);
    cic.power(CicModel::N7101);
    boot(cic, true, 0x3f, 0xa536c0f1d859);
  }
  {
    Cic cic(CicModel::N6105);
    boot(cic, false, 0x91, 0x8618a45bc2d3);
    cic.write_bit(true);
    cic.write_bit(false);
    equal(cic.read_nibble(), 10);
    equal(cic.read_nibble(), 10);
    for (unsigned n = 0; n < 30; ++n)
      cic.write_nibble(0);
    equal(cic.read_bit(), false);
    equal(cic.read_nibble(), 11);
    equal(cic.read_nibble(), 15);
    for (unsigned n = 2; n < 30; ++n)
      equal(cic.read_nibble(), n & 1 ? 15 : 9);
  }
  {
    Cic cic(CicModel::N6103);
    boot(cic, false, 0x78, 0x586fd4709867);
    cic.power(CicModel::N7106);
    boot(cic, true, 0x85, 0x2bbad4e6eb74);
  }
}

} // namespace test
