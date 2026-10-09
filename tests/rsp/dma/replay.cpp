#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
namespace {
struct Probe {
  RspFixture f;
  bool native = false;
  unsigned block = 0;
  std::uint64_t hash = 0xcbf29ce484222325ull, observations = 0;
  std::vector<unsigned> watched;

  Probe() {
    f.initialize();
  }
  void begin(bool mode) {
    native = mode;
    f.rsp.power();
    watched.clear();
  }
  void watch_ram(unsigned address) {
    watched.push_back(address);
  }
  void watched_ram() {
    for (auto address : watched) {
      emit(raw(address));
      if (address < f.ram.size())
        for (unsigned half = 0; half < 4; ++half)
          emit(f.ram.hidden()[address / 2 + half]);
      else
        for (unsigned half = 0; half < 4; ++half)
          emit(0);
    }
  }
  void local_write(unsigned address, std::uint64_t value) {
    f.rsp.write_local(address, 8, value);
  }
  auto local_read(unsigned address) {
    return f.rsp.read_local(address, 8);
  }
  void store(unsigned address, std::uint64_t value) {
    f.ram.write(address, 8, value);
  }
  std::uint64_t raw(unsigned address) {
    return f.ram.read(address, 8);
  }
  void io_write(unsigned address, unsigned value, int difference = 0) {
    f.rsp.write_io(address, value, difference);
  }
  auto io_read(unsigned address) {
    return f.rsp.read_io(address);
  }
  void tick_dma(unsigned clocks) {
    f.rsp.advance_dma(clocks);
  }
  auto dma_clock() {
    return f.rsp.dma_clocks();
  }
  auto clock() {
    return f.rsp.clocks();
  }
  auto pc() {
    return f.rsp.pc();
  }
  void code(unsigned address, unsigned instruction) {
    f.rsp.write_local(0x1000 | (address & 0xfff), 4, instruction);
  }
  void set_pc(unsigned address) {
    f.rsp.write_status(0, address);
  }
  void set_reg(unsigned reg, unsigned value) {
    f.rsp.state().gpr[reg] = value;
  }
  void set_vector(unsigned reg, unsigned lane, unsigned value) {
    f.rsp.state().vectors[reg].lanes[lane] = static_cast<std::uint16_t>(value);
  }
  void set_accumulator(unsigned lane, std::uint64_t value) {
    f.rsp.state().accumulator.set(lane, value);
  }
  void set_flags(std::uint64_t flags, unsigned input, unsigned output, bool pending) {
    auto &s = f.rsp.state();
    s.carry_low = static_cast<std::uint8_t>(flags);
    s.carry_high = static_cast<std::uint8_t>(flags >> 8);
    s.compare_low = static_cast<std::uint8_t>(flags >> 16);
    s.compare_high = static_cast<std::uint8_t>(flags >> 24);
    s.extension = static_cast<std::uint8_t>(flags >> 32);
    s.divide_input = static_cast<std::uint16_t>(input);
    s.divide_output = static_cast<std::uint16_t>(output);
    s.divide_double = pending;
  }
  void run(unsigned clocks) {
    f.rsp.elapse(clocks);
    if (native)
      f.rsp.run();
    else
      f.rsp.run_interpreted();
  }
  void registers() {
    const auto &s = f.rsp.state();
    for (auto value : s.gpr)
      emit(value);
    for (const auto &reg : s.vectors)
      for (auto value : reg.lanes)
        emit(value);
    for (unsigned lane = 0; lane < 8; ++lane)
      emit(s.accumulator.get(lane));
    emit(s.carry_low);
    emit(s.carry_high);
    emit(s.compare_low);
    emit(s.compare_high);
    emit(s.extension);
    emit(s.divide_input);
    emit(s.divide_output);
    emit(s.divide_double);
  }
  void emit(std::uint64_t value) {
    for (unsigned byte = 0; byte < 8; ++byte) {
      hash ^= (value >> (byte * 8)) & 255;
      hash *= 0x100000001b3ull;
    }
    ++observations;
  }
  void finish() {
    equal(block < rsp_dma::expected.size(), true);
    if (block < rsp_dma::expected.size()) {
      if (hash != rsp_dma::expected[block])
        std::cerr << "RSP DMA case " << block << '\n';
      equal(hash, rsp_dma::expected[block]);
    }
    ++block;
    hash = 0xcbf29ce484222325ull;
  }
};
} // namespace

void rsp_dma_replay_tests() {
  Probe p;
  rsp_dma::scenarios(p);
  equal(p.block, rsp_dma::expected.size());
  equal(p.observations, rsp_dma::observations);
}
} // namespace test
