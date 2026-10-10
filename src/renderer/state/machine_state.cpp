#include "renderer/state/machine_state.hpp"
#include "core/state/core_state.hpp"
#include "renderer/state/renderer_state.hpp"

namespace cupid::n64 {

std::vector<std::uint8_t> MachineState::capture(Console &console, HardwareRenderer &renderer) {
  RendererState::require_memory(renderer, console.ram());
  RendererState::fence(renderer);
  const auto core = CoreState::capture(console);
  const auto video = RendererState::capture(renderer);
  state::Archive archive;
  archive.identity<std::uint64_t>(0x45544154534d5043ull);
  archive.identity<std::uint32_t>(1);
  archive.owned_vector(core, 256 * 1024 * 1024);
  archive.owned_vector(video, 128 * 1024 * 1024);
  return archive.finish();
}

StateCheckpoint MachineState::checkpoint(Console &console, HardwareRenderer &renderer) {
  RendererState::require_memory(renderer, console.ram());
  RendererState::fence(renderer);
  auto core = CoreState::checkpoint(console);
  auto video = RendererState::checkpoint(renderer);
  state::Archive archive;
  archive.identity<std::uint64_t>(0x45544154534d5043ull);
  archive.identity<std::uint32_t>(1);
  archive.owned_vector(core.bytes, 256 * 1024 * 1024);
  archive.owned_vector(video.bytes, 128 * 1024 * 1024);
  std::vector<state::StateRange> ranges{{"machine.snapshot_identity", 0, 12, 1}};
  std::size_t offset = 12;
  const auto append = [&](StateCheckpoint &checkpoint, std::string_view owner) {
    ranges.push_back({std::string(owner) + ".snapshot_size", offset, 4, 4});
    offset += 4;
    for (auto &range : checkpoint.ranges) {
      range.offset += offset;
      ranges.push_back(std::move(range));
    }
    offset += checkpoint.bytes.size() - 8;
    ranges.push_back({std::string(owner) + ".snapshot_checksum", offset, 8, 8});
    offset += 8;
  };
  append(core, "core");
  append(video, "gpu");
  return {archive.finish(), std::move(ranges)};
}

void MachineState::restore(Console &console, HardwareRenderer &renderer,
                           std::span<const std::uint8_t> data) {
  RendererState::require_memory(renderer, console.ram());
  state::Archive archive(data);
  archive.identity<std::uint64_t>(0x45544154534d5043ull);
  archive.identity<std::uint32_t>(1);
  const auto core = archive.owned_vector<std::uint8_t>({}, 256 * 1024 * 1024);
  const auto video = archive.owned_vector<std::uint8_t>({}, 128 * 1024 * 1024);
  archive.validate();
  auto core_plan = CoreState::prepare(console, core);
  auto video_plan = RendererState::prepare(renderer, video);
  RendererState::fence(renderer);
  core_plan->finish();
  video_plan->finish();
}

} // namespace cupid::n64
