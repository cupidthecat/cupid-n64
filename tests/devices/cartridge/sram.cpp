#include "core/cartridge/sram.hpp"
#include "../fixture.hpp"
#include <algorithm>

namespace test {
using namespace cupid::n64;

void sram_tests() {
  for (unsigned size : {0u, 1u, 2u, 3u, 32769u}) {
    Sram ram(size);
    equal(ram.data().empty(), true);
    equal(ram.select(0x08000000, {}), false);
  }
  for (unsigned size : {512u, 32768u, 65536u, 98304u, 131072u}) {
    Sram ram(size);
    equal(ram.data().size(), size);
    equal(ram.select(0x07fffffe, {}), false);
    equal(ram.select(0x10000000, {}), false);
    const auto banks = size > 32768 ? size / 32768 : 1;
    const auto window = size > 32768 ? 32768 : size;
    for (unsigned bank = 0; bank < banks; ++bank) {
      const auto address = 0x08000000 + (bank << 18);
      equal(ram.select(address, {}), true);
      equal(ram.read_half({}).value_or(0), 0xffff);
      equal(ram.select(address, {}), true);
      ram.write_half(static_cast<std::uint16_t>(0x1234 + bank), {});
      equal(ram.select(address, {}), true);
      equal(ram.read_half({}).value_or(0), 0x1234 + bank);
      equal(ram.select(address + window - 2, {}), true);
      ram.write_half(0xabcd, {});
      ram.write_half(0x5678, {});
      equal(ram.select(address + window - 2, {}), true);
      equal(ram.read_half({}).value_or(0), 0xabcd);
      equal(ram.read_half({}).has_value(), false);
      equal(ram.data()[bank * window], 0x12);
      equal(ram.data()[bank * window + 1], 0x34 + bank);
      if (size > 32768)
        equal(ram.select(address + window, {}), false);
      else {
        equal(ram.select(address + window * 7, {}), true);
        equal(ram.read_half({}).value_or(0), 0x1234);
      }
    }
    if (size > 32768)
      equal(ram.select(0x08000000 + (banks << 18), {}), false);
    Sram restored(size);
    std::copy(ram.data().begin(), ram.data().end(), restored.data().begin());
    equal(std::equal(ram.data().begin(), ram.data().end(), restored.data().begin()), true);
  }
}

} // namespace test
