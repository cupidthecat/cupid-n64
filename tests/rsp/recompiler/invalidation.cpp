#include "core/rsp/recompiler/invalidation.hpp"
#include "../fixture.hpp"
#include "invalidation/cases.hpp"
#include <memory>

namespace test {
using namespace cupid::n64;

void rsp_invalidation_tests() {
  std::uint64_t digest = 14695981039346656037ull;
  for (unsigned address = 0; address < 4096; ++address)
    for (unsigned bytes = 0; bytes < 4096; ++bytes)
      digest = (digest ^ rsp_cache::line_mask(address, bytes)) * 1099511628211ull;
  equal(digest, 0xc3233ab0b23ed4a5ull);

  using invalidation::WritePath;
  for (const auto &[path, pc, address, ticks] : invalidation::cases) {
    auto fixture = std::make_unique<RspFixture>();
    auto &f = *fixture;
    f.initialize();
    auto &rsp = f.rsp;
    rsp.write_local(0, 4, 41);
    rsp.write_word(0x1000 | pc, 0x243f0001);
    rsp.write_word(0x1000 | ((pc + 4) & 0xfff), 0x08000000 | (pc >> 2));
    rsp.write_word(0x1000 | ((pc + 8) & 0xfff), 0x8c010000);
    rsp.write_status(0, pc);
    rsp.write_io(16, 1);
    rsp.advance(1);
    equal(rsp.clocks() + 1, 12);
    rsp.write_word(0x1400, 0x8c010000);
    rsp.write_word(0x1404, 0);
    rsp.write_status(0, 0x400);
    rsp.step();
    rsp.step();
    if (address != 0xffffffff) {
      const auto region = path == WritePath::DataWord || path == WritePath::DataDma ? 0 : 0x1000;
      if (path == WritePath::InstructionWord || path == WritePath::DataWord)
        rsp.write_word(region | address, 0);
      else {
        const bool full = path == WritePath::FullInstructionDma;
        const unsigned bytes = full ? 4096 : 8;
        const auto destination = address & 0xff8;
        for (unsigned offset = 0; offset < bytes; offset += 4) {
          const auto word = full ? rsp.read_local(0x1000 | ((destination + offset) & 0xfff), 4) : 0;
          f.ram.write(0x300 + offset, 4, word);
        }
        rsp.write_io(0, region | address);
        rsp.write_io(4, 0x300);
        rsp.write_io(8, bytes - 8);
        rsp.advance_dma(bytes / 8 * 3);
        equal(rsp.dma_busy(), false);
      }
    }
    equal(rsp.read_local(0x1000 | pc, 4), 0x243f0001);
    equal(rsp.read_local(0x1000 | ((pc + 4) & 0xfff), 4), 0x08000000 | (pc >> 2));
    equal(rsp.read_local(0x1000 | ((pc + 8) & 0xfff), 4), 0x8c010000);
    rsp.write_status(0, pc);
    rsp.advance(static_cast<unsigned>(rsp.clocks() + 1));
    equal(rsp.clocks() + 1, ticks);
    equal(rsp.pc(), pc);
    equal(rsp.state().gpr[1], 41);
    equal(rsp.state().gpr[31], 42);
    equal(rsp.status().halted, false);
    equal(rsp.status().broken, false);
  }
}

} // namespace test
