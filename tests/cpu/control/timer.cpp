#include "../native_memory_fixture.hpp"
#include "core/system/console.hpp"

namespace test {
using namespace cupid::n64;
namespace {

void commit_boundaries() {
  auto machine = std::make_unique<Console>();
  constexpr std::uint32_t initial[] = {0, 0xfffffffe, 0xffffffff};
  constexpr std::uint32_t compare[] = {0, 1, 2, 0xffffffff};
  constexpr unsigned deadlines[3][4] = {{0, 4, 8, 0}, {8, 12, 16, 4}, {4, 8, 12, 0}};
  for (unsigned start = 0; start < 3; ++start) {
    for (unsigned target = 0; target < 4; ++target) {
      for (unsigned clocks : {0u, 1u, 3u, 4u, 7u, 8u, 11u, 16u}) {
        for (unsigned action = 0; action < 5; ++action) {
          machine->power();
          auto &cpu = machine->cpu();
          cpu.write_control(Status, 0x34000000);
          cpu.write_control(Count, initial[start]);
          cpu.write_control(Compare, compare[target]);
          cpu.advance_clocks(clocks);
          equal(cpu.read_control(Cause) & 0x8000, 0);
          equal(cpu.read_control(Count), std::uint32_t(initial[start] + clocks / 4));
          equal(cpu.read_control(Cause) & 0x8000, 0);
          const bool transfer = action == 1 || action == 2;
          if (transfer)
            cpu.execute(c(action == 1 ? 0 : 1, 2, Count));
          if (action == 3)
            cpu.write_control(Count, initial[start]);
          if (action == 4)
            cpu.write_control(Compare, compare[target]);
          const auto elapsed = clocks + (transfer ? 2 : 0);
          const auto deadline = deadlines[start][target];
          const unsigned pending = deadline && elapsed >= deadline ? 0x8000 : 0;
          equal(cpu.read_control(Cause) & 0x8000, action != 0 && action != 4 ? pending : 0);
          machine->synchronize();
          equal(cpu.read_control(Cause) & 0x8000, action == 4 ? 0 : pending);
          equal(cpu.read_control(Count),
                std::uint32_t(initial[start] + (action == 3 ? 0 : elapsed / 4)));
        }
      }
    }
  }
}

void odd_synchronization() {
  auto machine = std::make_unique<Console>();
  for (unsigned clocks : {1u, 3u}) {
    machine->power();
    auto &cpu = machine->cpu();
    cpu.write_control(Count, 0);
    cpu.write_control(Compare, 1);
    for (unsigned commit = 1; commit <= 4; ++commit) {
      cpu.advance_clocks(clocks);
      equal(cpu.read_control(Cause) & 0x8000, clocks == 3 && commit > 2 ? 0x8000 : 0);
      machine->synchronize();
      equal(cpu.read_control(Count), clocks == 1 ? 0 : commit / 2);
      equal(cpu.read_control(Cause) & 0x8000, clocks == 3 && commit >= 2 ? 0x8000 : 0);
    }
  }
}

void native_count_read() {
  for (bool native : {false, true}) {
    for (unsigned transfer : {0u, 1u}) {
      native_memory::CachedFixture f;
      f.cpu.write_control(Status, 0x34000000);
      f.cpu.write_control(Count, 0);
      f.cpu.write_control(Compare, 1);
      f.cpu.state().gpr[31] = 0xffffffff80003000;
      const std::uint32_t code[] = {c(0, 8, Cause), c(transfer, 9, Count), c(0, 10, Cause),
                                    r(8, 31, 0, 0), 0};
      for (unsigned word = 0; word < 5; ++word)
        f.code(word * 4, code[word]);
      const auto start = f.cpu.state().clocks;
      equal(native ? f.cpu.run_block(start + 10000) : f.cpu.run_interpreted_block(start + 10000),
            true);
      equal(f.cpu.state().gpr[8], 0);
      equal(f.cpu.state().gpr[9], 24);
      equal(f.cpu.state().gpr[10], 0x8000);
      equal(f.cpu.state().clocks - start, 106);
      equal(f.cpu.read_control(Count), 26);
    }
  }
}

void device_order() {
  auto machine = std::make_unique<Console>();
  for (unsigned action = 0; action < 3; ++action) {
    machine->power();
    auto &cpu = machine->cpu();
    auto &video = machine->video();
    video.write_word(0, 2);
    video.write_word(24, 525);
    video.write_word(28, 256);
    video.write_word(32, 256);
    video.write_word(40, 1 << 17);
    unsigned frames = 0;
    video.connect_frame([&](bool) {
      ++frames;
      equal(cpu.read_control(Cause) & 0x8000, 0);
      equal(cpu.read_control(Count), 0);
      if (action == 1)
        cpu.write_control(Count, 10);
      if (action == 2)
        cpu.write_control(Compare, 3);
    });
    cpu.write_control(Count, 0);
    cpu.write_control(Compare, 2);
    cpu.advance_clocks(8);
    machine->synchronize();
    equal(frames, 1);
    equal(cpu.read_control(Count), action == 1 ? 12 : 2);
    equal(cpu.read_control(Cause) & 0x8000, action ? 0 : 0x8000);
  }
}

void dispatch_budget() {
  auto machine = std::make_unique<Console>();
  auto &cpu = machine->cpu();
  constexpr std::uint64_t period = 1ull << 33;
  cpu.write_control(Count, 100);
  cpu.write_control(Compare, 50);
  equal(cpu.synchronization_limit(), period - 100);
  cpu.advance_clocks(40);
  equal(cpu.synchronization_limit(), period - 120);
  machine->synchronize();
  equal(cpu.read_control(Cause) & 0x8000, 0);
  equal(cpu.synchronization_limit(), period - 120);

  cpu.write_control(Count, 50);
  cpu.write_control(Compare, 50);
  equal(cpu.synchronization_limit(), period);
  cpu.advance_clocks(4);
  machine->synchronize();
  equal(cpu.read_control(Cause) & 0x8000, 0);
  equal(cpu.synchronization_limit(), period - 2);

  for (unsigned start : {0u, 0xffffffffu}) {
    cpu.write_control(Count, start);
    cpu.write_control(Compare, start + 1);
    equal(cpu.synchronization_limit(), 2);
    cpu.advance_clocks(3);
    equal(cpu.synchronization_limit(), 1);
    cpu.advance_clocks(1);
    equal(cpu.synchronization_limit(), 0);
    equal(cpu.read_control(Cause) & 0x8000, 0);
    machine->synchronize();
    equal(cpu.read_control(Cause) & 0x8000, 0x8000);
    equal(cpu.synchronization_limit(), period);
    cpu.advance_clocks(8);
    equal(cpu.synchronization_limit(), period - 4);
    machine->synchronize();
    equal(cpu.read_control(Cause) & 0x8000, 0x8000);
    cpu.write_control(Compare, start + 8);
    equal(cpu.read_control(Cause) & 0x8000, 0);
    equal(cpu.synchronization_limit(), 10);
  }
}

} // namespace

void timer_phase_tests() {
  commit_boundaries();
  odd_synchronization();
  native_count_read();
  device_order();
  dispatch_budget();
}

} // namespace test
