#include "../renderer/replay/commands.hpp"
#include "../renderer/replay/fixture.hpp"
#include "core/state/archive.hpp"
#include "core/state/core_state.hpp"
#include "renderer/state/machine_state.hpp"
#include "renderer/state/renderer_state.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_set>

using namespace cupid::n64;
using namespace test::renderer_replay;

namespace {

void checkpoint_layout(const StateCheckpoint &checkpoint) {
  std::unordered_set<std::string> names;
  std::size_t end = 0;
  for (const auto &range : checkpoint.ranges) {
    test::equal(range.offset, end);
    const auto unique = names.insert(range.name).second;
    if (!unique)
      std::cerr << "Repeated machine checkpoint field " << range.name << '\n';
    test::equal(unique, true);
    test::equal(range.element_bytes > 0 && range.element_bytes <= 8, true);
    if (range.element_bytes)
      test::equal(range.bytes % range.element_bytes, 0);
    end += range.bytes;
  }
  test::equal(end + 8, checkpoint.bytes.size());
}

void same_checkpoint(const StateCheckpoint &expected, GpuFixture &fixture) {
  const auto difference =
      expected.difference(MachineState::checkpoint(fixture.console, *fixture.renderer));
  if (difference)
    std::cerr << "GPU continuation " << difference->field << '[' << difference->index
              << "] expected " << difference->expected << ", got " << difference->actual << '\n';
  test::equal(difference.has_value(), false);
}

void gpu_checkpoint_faults(const StateCheckpoint &checkpoint) {
  auto actual = checkpoint;
  unsigned tested = 0;
  for (const auto &range : checkpoint.ranges) {
    if (!range.name.starts_with("gpu.") || !range.bytes)
      continue;
    ++tested;
    for (const auto byte : {std::size_t(0), range.bytes - 1}) {
      actual.bytes[range.offset + byte] ^= 1;
      const auto difference = checkpoint.difference(actual);
      test::equal(difference.has_value(), true);
      if (difference) {
        test::equal(difference->field == range.name, true);
        test::equal(difference->index, byte / range.element_bytes);
        test::equal(difference->expected ^ difference->actual,
                    std::uint64_t(1) << ((byte % range.element_bytes) * 8));
      }
      actual.bytes[range.offset + byte] ^= 1;
    }
  }
  test::equal(tested > 200, true);
}

void same_frame(const VideoFrame &actual, const VideoFrame &expected) {
  test::equal(actual.width, expected.width);
  test::equal(actual.height, expected.height);
  test::equal(actual.rgba == expected.rgba, true);
}

void upload(GpuFixture &fixture, std::span<const RdpPacket> packets, bool xbus) {
  const unsigned base = xbus ? 0x1ff8 : 0x2000;
  for (unsigned n = 0; n < packets.size(); ++n)
    for (unsigned half = 0; half < 2; ++half)
      fixture.command_word(base + n * 8 + half * 4, packets[n][half], xbus);
  if (xbus)
    fixture.display_write(3, 2);
  fixture.display_write(0, base);
}

void perturb(GpuFixture &fixture) {
  fixture.reset_commands();
  const RenderCase overwrite{"overwrite", 3, 14, 4, 2, 1, 1, 0, false, false, 7};
  initialize_render(fixture, overwrite);
  for (unsigned offset = 0; offset < 256; offset += 4)
    fixture.put(texture_address + offset, 0x14253647u ^ offset);
  for (unsigned offset = 0; offset < 512; offset += 4)
    fixture.put(palette_address + offset, 0x7ff07ff0u ^ offset);
  unsigned draw = 0;
  auto commands = render_commands(overwrite, draw);
  commands.push_back({0x2a123456, 0x789abcde});
  commands.push_back({0x2b000789, 0x00abcdef});
  commands.push_back({0x2c008040, 0x20100806});
  commands.push_back({0x3a001f49, 0x12ab34cd});
  upload(fixture, commands, false);
  fixture.display_write(1, 0x2000 + static_cast<unsigned>(commands.size()) * 8);
  fixture.renderer->frame(true);
  fixture.put(texture_address, 0xabcdef01);
  fixture.hidden(color_address / 2, 0);
}

void continuation(GpuFixture &fixture, const RenderCase &input, bool xbus, bool ready) {
  fixture.reset_commands();
  initialize_render(fixture, input);
  unsigned draw = 0;
  auto commands = render_commands(input, draw);
  commands.push_back({0x29000000, 0});
  upload(fixture, commands, xbus);
  const unsigned base = xbus ? 0x1ff8 : 0x2000;
  const unsigned prefix = draw + (input.opcode == 0x36 ? 0 : 1);
  const unsigned end = base + static_cast<unsigned>(commands.size()) * 8;
  fixture.display_write(1, base + prefix * 8);
  fixture.console.display().advance(17);
  if (ready)
    fixture.renderer->begin_frame(false);
  else
    fixture.renderer->frame(false);
  const auto snapshot = MachineState::capture(fixture.console, *fixture.renderer);
  const auto checkpoint = MachineState::checkpoint(fixture.console, *fixture.renderer);
  checkpoint_layout(checkpoint);
  test::equal(checkpoint.bytes == snapshot, true);
  gpu_checkpoint_faults(checkpoint);
  const auto initial_frame = fixture.renderer->read_frame();
  test::equal(initial_frame.rgba.empty(), !ready);
  fixture.display_write(1, end);
  fixture.console.display().advance(31);
  const auto future_frame = fixture.renderer->frame(true);
  test::equal(future_frame.rgba.empty(), false);
  test::equal(fixture.crashed(), false);
  const auto future = MachineState::capture(fixture.console, *fixture.renderer);
  const auto future_checkpoint = MachineState::checkpoint(fixture.console, *fixture.renderer);
  test::equal(future_checkpoint.bytes == future, true);

  perturb(fixture);
  MachineState::restore(fixture.console, *fixture.renderer, snapshot);
  test::equal(MachineState::capture(fixture.console, *fixture.renderer) == snapshot, true);
  same_checkpoint(checkpoint, fixture);
  same_frame(fixture.renderer->read_frame(), initial_frame);
  fixture.display_write(1, end);
  fixture.console.display().advance(31);
  same_frame(fixture.renderer->frame(true), future_frame);
  test::equal(MachineState::capture(fixture.console, *fixture.renderer) == future, true);
  same_checkpoint(future_checkpoint, fixture);
}

void continuation_tests() {
  GpuFixture fixture;
  const RenderCase scenes[] = {{"fill", 2, 0x36, 0, 0, 3},
                               {"rgba-texture", 3, 14, 1, 2},
                               {"palette-texture", 2, 8, 3, 1, 0, 1, 0, true},
                               {"yuv-texture", 2, 14, 2, 2},
                               {"noise", 3, 14, 0, 1, 0, 0, 0, false, false, 4},
                               {"depth-layer", 2, 9, 0, 0, 0, 0, 0x430, false, false, 0, 2}};
  for (const auto &scene : scenes)
    for (bool xbus : {false, true})
      for (bool ready : {false, true}) {
        const auto before = test::failures;
        continuation(fixture, scene, xbus, ready);
        if (test::failures != before)
          std::cerr << "Snapshot continuation: " << scene.name << " xbus " << xbus << " ready "
                    << ready << '\n';
      }
}

void image_persistence_tests() {
  GpuFixture fixture;
  const RenderCase input{"fill", 2, 0x36, 0, 0, 3};
  initialize_render(fixture, input);
  unsigned draw = 0;
  const auto commands = render_commands(input, draw);
  upload(fixture, commands, false);
  fixture.display_write(1, 0x2000 + static_cast<unsigned>(commands.size()) * 8);
  const auto first = fixture.renderer->frame(false);
  const auto snapshot = MachineState::capture(fixture.console, *fixture.renderer);
  fixture.console.video().write_word(36, (108u << 16) | 108);
  const auto invalid = fixture.renderer->frame(true);
  test::equal(invalid.width > 1, true);
  same_frame(invalid, first);
  const auto future = MachineState::capture(fixture.console, *fixture.renderer);
  perturb(fixture);
  MachineState::restore(fixture.console, *fixture.renderer, snapshot);
  test::equal(fixture.renderer->read_frame().rgba.empty(), true);
  fixture.console.video().write_word(36, (108u << 16) | 108);
  same_frame(fixture.renderer->frame(true), invalid);
  test::equal(MachineState::capture(fixture.console, *fixture.renderer) == future, true);

  fixture.renderer.reset();
  GpuFixture fresh;
  MachineState::restore(fresh.console, *fresh.renderer, snapshot);
  test::equal(MachineState::capture(fresh.console, *fresh.renderer) == snapshot, true);
  fresh.console.video().write_word(36, (108u << 16) | 108);
  same_frame(fresh.renderer->frame(true), invalid);
  test::equal(MachineState::capture(fresh.console, *fresh.renderer) == future, true);
}

void checksum(std::vector<std::uint8_t> &bytes) {
  const auto value = state::Archive::fingerprint(std::span(bytes).first(bytes.size() - 8));
  for (unsigned n = 0; n < 8; ++n)
    bytes[bytes.size() - 8 + n] = static_cast<std::uint8_t>(value >> (n * 8));
}

bool rejects(GpuFixture &fixture, std::span<const std::uint8_t> bytes) {
  try {
    MachineState::restore(fixture.console, *fixture.renderer, bytes);
  } catch (const state::InvalidState &) {
    return true;
  }
  return false;
}

void invalid_tests() {
  GpuFixture fixture;
  perturb(fixture);
  const auto initial = MachineState::capture(fixture.console, *fixture.renderer);
  state::Archive reader(initial);
  reader.identity<std::uint64_t>(0x45544154534d5043ull);
  reader.identity<std::uint32_t>(1);
  const auto core = reader.owned_vector<std::uint8_t>({}, 256 * 1024 * 1024);
  const auto video = reader.owned_vector<std::uint8_t>({}, 128 * 1024 * 1024);
  reader.validate();
  for (unsigned fault = 0; fault < 6; ++fault) {
    auto broken_core = core, broken_video = video;
    if (fault == 0) {
      broken_core[8] = 4;
      checksum(broken_core);
    } else if (fault == 1) {
      broken_video[8] = 2;
      checksum(broken_video);
    } else if (fault == 2) {
      broken_video[27] = 0xff;
      checksum(broken_video);
    } else if (fault == 3) {
      broken_video.insert(broken_video.end() - 8, 0x42);
      checksum(broken_video);
    } else if (fault == 4) {
      broken_video.erase(broken_video.end() - 20, broken_video.end() - 8);
      checksum(broken_video);
    } else {
      broken_video.resize(20);
    }
    state::Archive writer;
    writer.identity<std::uint64_t>(0x45544154534d5043ull);
    writer.identity<std::uint32_t>(1);
    writer.owned_vector(broken_core, 256 * 1024 * 1024);
    writer.owned_vector(broken_video, 128 * 1024 * 1024);
    test::equal(rejects(fixture, writer.finish()), true);
    test::equal(MachineState::capture(fixture.console, *fixture.renderer) == initial, true);
  }
  Console other({.random_seed = 0});
  const auto other_initial = CoreState::capture(other);
  bool wrong_memory = false;
  try {
    MachineState::restore(other, *fixture.renderer, initial);
  } catch (const state::InvalidState &) {
    wrong_memory = true;
  }
  test::equal(wrong_memory, true);
  test::equal(CoreState::capture(other) == other_initial, true);
  test::equal(MachineState::capture(fixture.console, *fixture.renderer) == initial, true);
}

} // namespace

int main() {
  {
    auto console = std::make_unique<Console>(ConsoleConfig{.random_seed = 0});
    try {
      HardwareRenderer renderer(console->ram());
    } catch (const std::runtime_error &error) {
      std::cout << error.what() << '\n';
      return 77;
    }
  }
  try {
    continuation_tests();
    image_persistence_tests();
    invalid_tests();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
