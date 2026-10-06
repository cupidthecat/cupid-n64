#include "../support/test.hpp"
#include <array>

namespace test {
using namespace cupid::n64;

void memory_tests() {
  const std::array<std::uint64_t, 7> expected{
      0xffffffffffffff80, 0xffffffffffff80ff, 0xffffffff80ff7ffe, 0x80, 0x80ff,
      0x80ff7ffe,         0x80ff7ffe12345678};
  const std::array<unsigned, 7> operations{0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x37};
  for (unsigned j = 0; j < operations.size(); ++j) {
    Fixture f;
    f.memory.put(0x2000, 8, 0x80ff7ffe12345678);
    f.cpu.state().gpr[1] = 0xffffffffa0002000;
    f.cpu.execute(i(operations[j], 1, 2, 0));
    equal(f.cpu.state().gpr[2], expected[j]);
  }
  for (unsigned bytes : {1u, 2u, 4u, 8u}) {
    Fixture f;
    f.cpu.state().gpr[1] = 0xffffffffa0002000;
    f.cpu.state().gpr[2] = 0x0123456789abcdef;
    f.cpu.execute(i(bytes == 1 ? 0x28 : bytes == 2 ? 0x29 : bytes == 4 ? 0x2b : 0x3f, 1, 2, 0));
    equal(f.memory.get(0x2000, bytes),
          bytes == 8 ? 0x0123456789abcdef : 0x0123456789abcdefull & ((1ull << (bytes * 8)) - 1));
    equal(f.memory.transfers.size(), 1);
  }
  {
    Fixture f;
    f.cpu.state().gpr[1] = 0xffffffffa0002001;
    f.cpu.state().gpr[2] = 42;
    f.cpu.execute(i(0x23, 1, 2, 0));
    equal(f.exception(), 4);
    equal(f.cpu.state().gpr[2], 42);
    equal(f.memory.transfers.size(), 0);
    equal(f.cpu.read_control(BadVAddr), 0xffffffffa0002001);
    equal(f.cpu.read_control(EntryHi), 0xc00000ffa0002000);
  }
  {
    Fixture f;
    f.cpu.state().gpr[1] = 0x00000000a0002000;
    f.cpu.execute(i(0x2b, 1, 2, 0));
    equal(f.exception(), 5);
    equal(f.memory.transfers.size(), 0);
  }
  const std::array<std::uint64_t, 4> left32{0xffffffff81234567, 0x234567dd, 0x4567ccdd, 0x67bbccdd};
  const std::array<std::uint64_t, 4> right32{0x11223344aabbcc81, 0x11223344aabb8123,
                                             0x11223344aa812345, 0xffffffff81234567};
  for (unsigned offset = 0; offset < 4; ++offset) {
    for (bool left : {false, true}) {
      Fixture f;
      f.memory.put(0x2000, 4, 0x81234567);
      f.cpu.state().gpr[1] = 0xffffffffa0002000;
      f.cpu.state().gpr[2] = 0x11223344aabbccdd;
      f.cpu.execute(i(left ? 0x22 : 0x26, 1, 2, static_cast<std::uint16_t>(offset)));
      equal(f.cpu.state().gpr[2], left ? left32[offset] : right32[offset]);
    }
  }
  for (unsigned bytes : {4u, 8u}) {
    for (bool left : {false, true}) {
      for (unsigned offset = 0; offset < bytes; ++offset) {
        Fixture f;
        f.memory.put(0x2000, 8, 0xeeeeeeeeeeeeeeee);
        const auto value = bytes == 8 ? 0x0123456789abcdefull : 0x89abcdefull;
        f.cpu.state().gpr[1] = 0xffffffffa0002000;
        f.cpu.state().gpr[2] = value;
        f.cpu.execute(i(bytes == 8 ? (left ? 0x2c : 0x2d) : (left ? 0x2a : 0x2e), 1, 2,
                        static_cast<std::uint16_t>(offset)));
        for (unsigned position = 0; position < 8; ++position) {
          const bool affected =
              position < bytes && (left ? position >= offset : position <= offset);
          const auto source = left ? position - offset : bytes - 1 - offset + position;
          const auto wanted = affected ? (value >> ((bytes - 1 - source) * 8)) & 255 : 0xee;
          equal(f.memory.get(0x2000 + position, 1), wanted);
        }
      }
    }
  }
  for (unsigned bytes : {4u, 8u}) {
    for (bool left : {false, true}) {
      for (unsigned offset = 0; offset < bytes; ++offset) {
        Fixture f;
        f.cpu.write_control(Config, 0);
        f.memory.put(0x2000, 8, 0xeeeeeeeeeeeeeeee);
        const auto value = bytes == 8 ? 0x0123456789abcdefull : 0x89abcdefull;
        f.cpu.state().gpr[1] = 0xffffffffa0002000;
        f.cpu.state().gpr[2] = value;
        f.cpu.execute(i(bytes == 8 ? (left ? 0x2c : 0x2d) : (left ? 0x2a : 0x2e), 1, 2,
                        static_cast<std::uint16_t>(offset)));
        for (unsigned position = 0; position < 8; ++position) {
          const bool affected =
              position < bytes && (left ? position <= offset : position >= offset);
          const auto shift = left ? (bytes - 1 - offset + position) * 8 : (position - offset) * 8;
          const auto wanted = affected ? (value >> shift) & 255 : 0xee;
          equal(f.memory.get(0x2000 + (position ^ 7), 1), wanted);
        }
      }
    }
  }
  {
    Fixture f;
    f.cpu.state().gpr[1] = 0xffffffffa0002001;
    f.memory.put(0x2000, 8, 0x0123456789abcdef);
    f.cpu.execute(i(0x22, 1, 2, 0));
    f.cpu.execute(i(0x26, 1, 2, 3));
    equal(f.cpu.state().gpr[2], 0x23456789);
  }
  {
    Fixture f;
    f.cpu.state().gpr[1] = 0xffffffffa0002003;
    f.memory.put(0x2000, 8, 0x0123456789abcdef);
    f.memory.put(0x2008, 8, 0x1020304050607080);
    f.cpu.execute(i(0x1a, 1, 2, 0));
    f.cpu.execute(i(0x1b, 1, 2, 7));
    equal(f.cpu.state().gpr[2], 0x6789abcdef102030);
  }
  {
    Fixture f;
    f.cpu.state().gpr[1] = 0xffffffffa0002000;
    f.cpu.state().gpr[2] = 99;
    f.cpu.execute(i(0x38, 1, 2, 0));
    equal(f.cpu.state().gpr[2], 0);
    equal(f.memory.transfers.size(), 0);
    f.memory.put(0x2000, 4, 0x80000000);
    f.cpu.execute(i(0x30, 1, 2, 0));
    equal(f.cpu.state().gpr[2], 0xffffffff80000000);
    equal(f.cpu.read_control(LlAddr), 0x200);
    f.cpu.state().gpr[2] = 17;
    f.cpu.execute(i(0x38, 1, 2, 0));
    equal(f.cpu.state().gpr[2], 1);
    equal(f.memory.get(0x2000, 4), 17);
  }
  {
    Fixture f;
    f.memory.success = false;
    f.cpu.step();
    equal(f.exception(), 6);
    f.cpu.write_control(Status, 0);
    f.cpu.set_pc(0xffffffffa0001000);
    f.cpu.state().gpr[1] = 0xffffffffa0002000;
    f.cpu.state().gpr[2] = 33;
    f.cpu.execute(i(0x23, 1, 2, 0));
    equal(f.exception(), 7);
    equal(f.cpu.state().gpr[2], 33);
  }
}

} // namespace test
