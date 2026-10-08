#include "fixture.hpp"

namespace test {
using namespace cupid::n64;
namespace {

struct SequentialRsp : RspFixture {
  std::int64_t clock = 0;
  bool delay = false;

  static bool branch(std::uint32_t instruction) {
    const auto op = instruction >> 26;
    if (op == 0)
      return (instruction & 63) == 8 || (instruction & 63) == 9 || (instruction & 63) == 13;
    if (op == 1) {
      const auto condition = (instruction >> 16) & 31;
      return condition == 0 || condition == 1 || condition == 16 || condition == 17;
    }
    return op >= 2 && op <= 7;
  }

  bool taken(std::uint32_t instruction) const {
    const auto op = instruction >> 26;
    const auto rs = (instruction >> 21) & 31;
    const auto rt = (instruction >> 16) & 31;
    const auto a = rsp.state().gpr[rs];
    const auto b = rsp.state().gpr[rt];
    if (op == 0)
      return (instruction & 63) == 8 || (instruction & 63) == 9;
    if (op == 1)
      return (rt == 0 || rt == 16) ? std::int32_t(a) < 0
                                   : (rt == 1 || rt == 17) && std::int32_t(a) >= 0;
    if (op == 2 || op == 3)
      return true;
    if (op == 4)
      return a == b;
    if (op == 5)
      return a != b;
    if (op == 6)
      return std::int32_t(a) <= 0;
    if (op == 7)
      return std::int32_t(a) > 0;
    return false;
  }

  void advance(unsigned clocks) {
    clock -= clocks;
    while (clock < 0) {
      unsigned elapsed = 0;
      if (rsp.status().halted) {
        elapsed = 128;
      } else {
        const auto start = rsp.pc();
        bool slot = delay;
        do {
          const auto pc = rsp.pc();
          const auto first = static_cast<std::uint32_t>(rsp.read_local(0x1000 | pc, 4));
          const auto second =
              static_cast<std::uint32_t>(rsp.read_local(0x1000 | ((pc + 4) & 0xfff), 4));
          const auto first_taken = taken(first);
          const auto second_taken = taken(second);
          elapsed += rsp.step();
          const bool dual = !slot && !branch(first) && rsp.pc() == ((pc + 8) & 0xfff);
          delay = first_taken || (dual && second_taken);
          if (slot || rsp.status().halted || rsp.pc() == start)
            break;
          slot = branch(first) || (dual && branch(second));
        } while (true);
      }
      clock += elapsed;
      rsp.advance_dma(elapsed);
    }
  }
};

void compare(RspFixture &actual, SequentialRsp &expected) {
  const auto &a = actual.rsp.state();
  const auto &b = expected.rsp.state();
  equal(actual.rsp.pc(), expected.rsp.pc());
  equal(actual.rsp.clocks(), expected.clock);
  for (unsigned reg = 0; reg < 32; ++reg) {
    equal(a.gpr[reg], b.gpr[reg]);
    for (unsigned lane = 0; lane < 8; ++lane)
      equal(a.vectors[reg].lanes[lane], b.vectors[reg].lanes[lane]);
  }
  for (unsigned lane = 0; lane < 8; ++lane)
    equal(a.accumulator.get(lane), b.accumulator.get(lane));
  equal(a.carry_low, b.carry_low);
  equal(a.carry_high, b.carry_high);
  equal(a.compare_low, b.compare_low);
  equal(a.compare_high, b.compare_high);
  equal(a.extension, b.extension);
  equal(a.divide_input, b.divide_input);
  equal(a.divide_output, b.divide_output);
  equal(a.divide_double, b.divide_double);
  equal(actual.rsp.dma_busy(), expected.rsp.dma_busy());
  equal(actual.rsp.dma_clocks(), expected.rsp.dma_clocks());
  equal(actual.rsp.read_io(16), expected.rsp.read_io(16));
  equal(actual.mi.read_word(8), expected.mi.read_word(8));
  for (unsigned address = 0; address < 8192; address += 4)
    equal(actual.rsp.read_local(address, 4), expected.rsp.read_local(address, 4));
}

void run(RspFixture &actual, SequentialRsp &expected, unsigned clocks) {
  actual.rsp.advance(clocks);
  expected.advance(clocks);
  compare(actual, expected);
}

} // namespace

void rsp_native_tests() {
  for (unsigned operation : {32u, 33u, 35u, 36u, 37u, 39u, 40u, 41u, 43u}) {
    for (unsigned offset : {0u, 1u, 2u, 3u, 4u, 0xffcu, 0xffdu, 0xffeu, 0xfffu}) {
      for (unsigned target : {0u, 1u, 2u}) {
        RspFixture actual;
        SequentialRsp expected;
        for (auto *fixture : {&actual, static_cast<RspFixture *>(&expected)}) {
          for (unsigned address = 0; address < 4096; ++address)
            fixture->rsp.dmem()[address] = static_cast<std::uint8_t>(address * 73 + 0x81);
          fixture->rsp.state().gpr[1] = 0x1234f000 + offset + 7;
          fixture->rsp.state().gpr[2] = 0x90abcdef;
          fixture->rsp.write_local(0x1000, 4, i(operation, 1, target, 0xfff9));
          fixture->rsp.write_local(0x1004, 4, vector(63, 0, 0, 0));
          fixture->rsp.write_local(0x1008, 4, 13);
          fixture->rsp.write_io(16, 1);
        }
        run(actual, expected, 1);
        run(actual, expected, 16);
      }
    }
  }
  {
    RspFixture actual;
    SequentialRsp expected;
    for (auto *fixture : {&actual, static_cast<RspFixture *>(&expected)}) {
      fixture->rsp.write_local(0x1000, 4, i(9, 1, 1, 1));
      fixture->rsp.write_local(0x1004, 4, 0x08000000);
      fixture->rsp.write_local(0x1008, 4, 0);
      fixture->rsp.write_io(16, 1);
    }
    auto memory = actual.rsp.imem();
    run(actual, expected, 128);
    const auto changed = i(9, 1, 1, 7);
    for (unsigned byte = 0; byte < 4; ++byte)
      memory[byte] = static_cast<std::uint8_t>(changed >> ((3 - byte) * 8));
    expected.rsp.write_local(0x1000, 4, changed);
    run(actual, expected, 128);
  }
  {
    RspFixture actual;
    SequentialRsp expected;
    for (auto *fixture : {&actual, static_cast<RspFixture *>(&expected)}) {
      fixture->rsp.write_local(0x1000, 4, c(4, 1, 7));
      fixture->rsp.write_local(0x1004, 4, i(9, 0, 2, 1));
      fixture->rsp.write_local(0x1008, 4, 13);
      fixture->rsp.write_io(16, 1);
    }
    run(actual, expected, 3);
    equal(actual.rsp.pc(), 12);
    equal(actual.rsp.state().gpr[2], 1);
    equal(actual.rsp.status().halted, true);
  }
  std::uint64_t random = 0x5396543147aaull;
  const auto next = [&] {
    random ^= random << 13;
    random ^= random >> 7;
    random ^= random << 17;
    return random;
  };
  constexpr std::array functions{0u,  2u,  3u,  4u,  6u,  7u,  8u,  9u,  32u,
                                 33u, 34u, 35u, 36u, 37u, 38u, 39u, 42u, 43u};
  constexpr std::array operations{8u,  9u,  10u, 11u, 12u, 13u, 14u, 15u, 32u,
                                  33u, 35u, 36u, 37u, 39u, 40u, 41u, 43u};
  constexpr std::array vectors{0u,  4u,  5u,  6u,  7u,  8u,  12u, 13u, 14u, 15u, 16u,
                               17u, 20u, 21u, 29u, 32u, 33u, 34u, 35u, 36u, 37u, 38u,
                               39u, 40u, 42u, 44u, 48u, 49u, 50u, 51u, 55u, 63u};
  for (unsigned trial = 0; trial < 16; ++trial) {
    RspFixture actual;
    SequentialRsp expected;
    const auto start = trial & 1 ? 0xffcu : 0u;
    for (unsigned reg = 0; reg < 32; ++reg) {
      actual.rsp.state().gpr[reg] = reg ? static_cast<std::uint32_t>(next()) : 0;
      for (auto &lane : actual.rsp.state().vectors[reg].lanes)
        lane = static_cast<std::uint16_t>(next());
    }
    expected.rsp.state() = actual.rsp.state();
    for (unsigned word = 0; word < 64; ++word) {
      const auto rs = static_cast<unsigned>(next() & 31);
      const auto rt = static_cast<unsigned>(next() & 31);
      const auto rd = static_cast<unsigned>(next() & 31);
      std::uint32_t instruction;
      if (word % 5 == 0)
        instruction = vector(vectors[next() % vectors.size()], rd, rs, rt,
                             static_cast<unsigned>(next() & 15));
      else if (word % 5 == 1)
        instruction =
            r(functions[next() % functions.size()], rs, rt, rd, static_cast<unsigned>(next() & 31));
      else if (word % 5 == 2)
        instruction = i(4 + static_cast<unsigned>(next() & 3), rs, rt,
                        static_cast<std::uint16_t>(next() % 8));
      else
        instruction =
            i(operations[next() % operations.size()], rs, rt, static_cast<std::uint16_t>(next()));
      for (auto *fixture : {&actual, static_cast<RspFixture *>(&expected)})
        fixture->rsp.write_local(0x1000 | ((start + word * 4) & 0xfff), 4, instruction);
    }
    for (auto *fixture : {&actual, static_cast<RspFixture *>(&expected)}) {
      fixture->rsp.write_local(0x1000 | ((start + 256) & 0xfff), 4, 0x08000000 | (start >> 2));
      fixture->rsp.write_local(0x1000 | ((start + 260) & 0xfff), 4, 0);
      fixture->rsp.write_status(0, start);
      fixture->rsp.write_io(16, 1);
    }
    for (unsigned iteration = 0; iteration < 256; ++iteration) {
      const auto before = failures;
      // Compare newly compiled schedules; context tests cover cached schedule reuse.
      const auto address = 0x1000 | actual.rsp.pc();
      actual.rsp.write_local(address, 4, actual.rsp.read_local(address, 4));
      run(actual, expected, static_cast<unsigned>(actual.rsp.clocks()) + 1);
      if (failures != before)
        return;
      if (iteration == 31)
        for (auto *fixture : {&actual, static_cast<RspFixture *>(&expected)})
          fixture->rsp.write_local(0x1000 | start, 4, i(9, 0, 1, 79));
    }
    equal(actual.rsp.step(), expected.rsp.step());
    compare(actual, expected);
  }
  {
    RspFixture actual;
    SequentialRsp expected;
    for (auto *fixture : {&actual, static_cast<RspFixture *>(&expected)}) {
      fixture->initialize();
      fixture->ram.write(0, 4, 13);
      fixture->ram.write(4, 4, 0);
      fixture->rsp.state().gpr[1] = 0x1000;
      fixture->rsp.state().gpr[2] = 0;
      fixture->rsp.state().gpr[3] = 24;
      for (unsigned reg = 0; reg < 3; ++reg) {
        fixture->rsp.write_local(0x1000 + reg * 8, 4, c(4, reg + 1, reg));
        fixture->rsp.write_local(0x1004 + reg * 8, 4, vector(63, 0, 0, 0));
      }
      fixture->rsp.write_local(0x1018, 4, 0x08000000);
      fixture->rsp.write_local(0x101c, 4, 0);
      fixture->rsp.write_io(16, 1 | (1 << 8));
    }
    for (unsigned clocks : {1u, 2u, 3u, 4u, 5u, 7u, 9u, 127u})
      run(actual, expected, clocks);
    equal(actual.rsp.status().broken, true);
  }
}

} // namespace test
