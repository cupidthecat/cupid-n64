#include "../fixture.hpp"
#include "offset_expected.hpp"

namespace test {
using namespace cupid::n64;

namespace {
void mix(std::uint64_t &hash, std::uint64_t value) {
  for (unsigned byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 255;
    hash *= 0x100000001b3ull;
  }
}

void offsets(MemoryFixture &f) {
  unsigned index = 0;
  for (bool ebus : {false, true})
    for (unsigned bytes : {1u, 2u, 4u, 8u})
      for (unsigned offset = 0; offset < 8; ++offset) {
        f.mi.write_word(0, 0x200);
        for (unsigned word = 0; word < 16; ++word)
          f.mi.write_rdram(0x1200 + word * 4, 4, 0x12345678u + word * 0x1020401u);
        f.mi.write_word(0, ebus ? 0x400 : 0x200);
        std::uint64_t hash = 0xcbf29ce484222325ull;
        mix(hash, ebus);
        mix(hash, bytes);
        mix(hash, offset);
        for (unsigned read_bytes : {1u, 2u, 4u, 8u})
          mix(hash, f.mi.read_rdram(0x1200 + offset, read_bytes).value);
        f.mi.write_rdram(0x1200 + offset, bytes, 0x0123456789abcdefull);
        for (unsigned word = 0; word < 16; ++word)
          mix(hash, f.ram.read(0x1200 + word * 4, 4));
        for (unsigned half = 0; half < 32; ++half)
          mix(hash, f.ram.hidden()[0x900 + half]);
        equal(hash, memory_offsets::expected[index++]);
      }
}
} // namespace

void memory_word_pair_tests() {
  for (bool expansion : {false, true}) {
    MemoryFixture f(expansion);
    f.initialize();
    f.mi.write_rdram(0x1200, 4, 0x11223344);
    f.mi.write_rdram(0x1204, 4, 0x55667788);
    f.mi.write_rdram(0x1208, 4, 0x99aabbcc);
    equal(f.mi.read_rdram(0x1204, 8).value, 0x5566778899aabbcc);
    f.mi.write_rdram(0x1204, 8, 0x0123456789abcdef);
    equal(f.ram.read(0x1200, 4), 0x11223344);
    equal(f.ram.read(0x1204, 4), 0x01234567);
    equal(f.ram.read(0x1208, 4), 0x89abcdef);

    f.ram.write(0x1200, 8, 0);
    f.ram.write(0x1208, 8, 0);
    f.mi.write_rdram(0x1202, 4, 0x89abcdef);
    equal(f.ram.read(0x1200, 4), 0x89abcdef);
    equal(f.ram.hidden()[0x900], 0);
    equal(f.ram.hidden()[0x901], 3);
    equal(f.ram.hidden()[0x902], 3);

    f.mi.write_word(0, 0x400);
    f.mi.write_rdram(0x1200, 4, 5);
    f.mi.write_rdram(0x1204, 4, 9);
    f.mi.write_rdram(0x1208, 4, 12);
    equal(f.mi.read_rdram(0x1204, 8).value, 0x000000090000000c);
    f.mi.write_word(0, 0x200);

    f.cpu.state().gpr[1] = 0xffffffffa0001204;
    f.cpu.state().gpr[2] = 0x76543210;
    f.cpu.execute(i(55, 1, 2, 0));
    equal((f.cpu.read_control(Cause) >> 2) & 31, 4);
    equal(f.cpu.read_control(BadVAddr), 0xffffffffa0001204);
    equal(f.cpu.state().gpr[2], 0x76543210);
    f.cpu.write_control(Status, 0x30000000);
    f.cpu.set_pc(0xffffffffa0001000);
    f.cpu.execute(i(63, 1, 2, 0));
    equal((f.cpu.read_control(Cause) >> 2) & 31, 5);
    equal(f.cpu.read_control(BadVAddr), 0xffffffffa0001204);

    f.ram.write(0, 4, 0x11223344);
    f.ram.write(f.ram.size() - 4, 4, 0x55667788);
    equal(f.mi.read_rdram(f.ram.size() - 4, 8).value, 0x5566778811223344);
    equal(f.ram.read(f.ram.size(), 8), 0);

    offsets(f);
  }
}

} // namespace test
