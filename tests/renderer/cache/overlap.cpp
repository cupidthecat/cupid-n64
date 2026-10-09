#include "fixture.hpp"

namespace test {
using namespace renderer_cache;
namespace {

enum class Warmup { Invalidated, Resident, SeparateEntry, Cold };

std::vector<Packet> replacement(bool depth) {
  if (!depth)
    return {{0x3f18003f, target + 32},
            {0x2d000000, 0x00100100},
            {0x2f300000, 0},
            {0x37000000, i(9, 0, 4, 7)},
            {0x36000000, 0}};
  return {{0x3f10003f, 0x180000}, {0x3e000000, target + 32}, {0x2d000000, 0x00100100},
          {0x2f0000f0, 0x24},     {0x3c000000, 0},           {0x2e000000, 0x44040001},
          {0x36004004, 0},        {0x2e000000, 0x00081000},  {0x36008004, 0x00004000}};
}

void overlap_case(CacheFixture &fixture, bool native, bool depth, Warmup warm, unsigned completion,
                  unsigned operation) {
  const auto initial_failures = failures;
  auto &cpu = fixture.cpu;
  auto &ram = fixture.console.ram();
  constexpr auto entry = cached + 32;
  constexpr auto end = cached + 40;
  fixture.reset();
  for (unsigned n = 0; n < 16; ++n)
    ram.write(target + n * 4, 4, 0);
  ram.write(target, 4, i(9, 0, 4, 1));
  ram.write(target + 32, 4, i(9, 0, 4, 1));
  ram.write(target + 36, 4, c(4, 5, Status));
  cpu.set_pc(cached);
  if (warm != Warmup::Cold)
    fixture.execute(native, cached, end);
  if (warm == Warmup::Invalidated)
    fixture.maintain(16, entry);
  if (warm == Warmup::SeparateEntry)
    fixture.execute(native, entry, end);
  fixture.maintain(4, entry);
  const bool resident = warm == Warmup::Resident || warm == Warmup::SeparateEntry;
  equal((cpu.read_control(TagLo) & 0x80) != 0, resident);
  equal(fixture.load(entry), i(9, 0, 4, 1));
  auto *tracker = fixture.console.instruction_tracker();
  const auto generation = tracker->generation(target);
  const auto sample = [&](unsigned value, unsigned memory_value, unsigned cached_value,
                          unsigned hidden1, unsigned clocks) {
    equal(cpu.state().gpr[4], value);
    equal(fixture.load(entry), i(9, 0, 4, static_cast<std::uint16_t>(cached_value)));
    equal(fixture.load(direct + 32), i(9, 0, 4, static_cast<std::uint16_t>(memory_value)));
    equal(ram.read(target + 36, 4), c(4, 5, Status));
    equal(ram.hidden()[(target + 32) / 2] & 3, 0);
    equal(ram.hidden()[(target + 34) / 2] & 3, hidden1);
    equal(cpu.state().clocks, clocks);
    equal(cpu.read_control(Cause), 0);
  };
  unsigned clocks = warm == Warmup::Cold            ? 90
                    : warm == Warmup::Invalidated   ? 304
                    : warm == Warmup::SeparateEntry ? 306
                                                    : 302;
  sample(warm == Warmup::Cold ? 0 : 1, 1, 1, 3, clocks);

  if (completion >= 2) {
    fixture.submit(surface(0xf801f801), true);
    fixture.begin_frame();
  }
  auto packets = replacement(depth);
  const auto blue = surface(0x003f003f);
  packets.insert(packets.end(), blue.begin(), blue.end());
  fixture.submit(std::move(packets), completion == 0 || completion == 3);
  if (completion >= 2)
    equal(fixture.read_frame(), red_frame);
  fixture.begin_frame();
  equal(fixture.read_frame(), blue_frame);
  equal(tracker->generation(target), generation);

  const unsigned before = warm == Warmup::Cold || (!native && !resident) ? 7 : 1;
  fixture.execute(native, cached, end);
  clocks += warm == Warmup::Cold ? 218 : warm == Warmup::Invalidated ? 122 : 26;
  sample(before, 7, 1, depth ? 0 : 3, clocks);
  fixture.execute(native, warm == Warmup::SeparateEntry ? entry : cached, end);
  const unsigned repeated = warm == Warmup::SeparateEntry ? 10 : 26;
  clocks += repeated;
  sample(before, 7, 1, depth ? 0 : 3, clocks);

  fixture.maintain(operation, entry);
  const unsigned memory_value = operation == 24 && resident ? 1 : 7;
  const unsigned after =
      native ? (warm == Warmup::Cold || (warm == Warmup::Invalidated && operation == 24) ? 7 : 1)
             : (operation == 31 ? before : memory_value);
  fixture.execute(native, cached, end);
  clocks += operation == 31 ? 28 : 124;
  sample(after, memory_value, 1, operation == 24 || !depth ? 3 : 0, clocks);
  fixture.execute(native, warm == Warmup::SeparateEntry ? entry : cached, end);
  clocks += repeated;
  sample(after, memory_value, 1, operation == 24 || !depth ? 3 : 0, clocks);

  const bool cached_store = completion & 1;
  fixture.store(cached_store ? entry : direct + 32, i(9, 0, 4, 13));
  if (cached_store)
    fixture.maintain(21, entry);
  fixture.maintain(16, entry);
  fixture.execute(native, cached, end);
  clocks += cached_store ? 288 : 126;
  sample(13, 13, cached_store ? 13 : 1, 3, clocks);
  if (native)
    equal(tracker->generation(target) != generation, true);
  if (failures != initial_failures)
    std::cerr << "native=" << native << " depth=" << depth
              << " warm=" << static_cast<unsigned>(warm) << " completion=" << completion
              << " operation=" << operation << '\n';
}

} // namespace

void gpu_cache_overlap_tests(Console &console, HardwareRenderer &renderer) {
  CacheFixture fixture{console, renderer};
  for (bool native : {false, true})
    for (bool depth : {false, true})
      for (auto warm : {Warmup::Invalidated, Warmup::Resident, Warmup::SeparateEntry, Warmup::Cold})
        for (unsigned completion : {0u, 1u, 2u, 3u})
          for (unsigned operation : {0u, 8u, 16u, 20u, 24u, 31u})
            overlap_case(fixture, native, depth, warm, completion, operation);
}

} // namespace test
