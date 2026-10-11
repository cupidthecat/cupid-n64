#include "renderer/state/machine_state.hpp"
#include "core/state/core_state.hpp"
#include "renderer/state/renderer_state.hpp"

namespace cupid::n64 {
namespace {

constexpr std::uint32_t core_limit = 256 * 1024 * 1024;
constexpr std::uint32_t video_limit = 128 * 1024 * 1024;

void envelope_identity(state::Archive &archive) {
  archive.label("machine.snapshot_identity");
  archive.identity<std::uint64_t>(0x45544154534d5043ull);
  archive.identity<std::uint32_t>(1);
}

void write_envelope(state::Archive &archive, const std::vector<std::uint8_t> &core,
                    const std::vector<std::uint8_t> &video) {
  envelope_identity(archive);
  archive.owned_vector(core, core_limit, "core.snapshot");
  archive.owned_vector(video, video_limit, "gpu.snapshot");
}

} // namespace

std::vector<std::uint8_t> MachineState::capture(Console &console, HardwareRenderer &renderer) {
  RendererState::require_memory(renderer, console.ram());
  RendererState::fence(renderer);
  const auto core = CoreState::capture(console);
  const auto video = RendererState::capture(renderer);
  state::Archive archive;
  write_envelope(archive, core, video);
  return archive.finish();
}

StateCheckpoint MachineState::checkpoint(Console &console, HardwareRenderer &renderer) {
  RendererState::require_memory(renderer, console.ram());
  RendererState::fence(renderer);
  auto core = CoreState::checkpoint(console);
  auto video = RendererState::checkpoint(renderer);
  state::Archive archive(true);
  write_envelope(archive, core.bytes, video.bytes);
  std::vector<state::StateRange> ranges;
  const auto append = [&](StateCheckpoint &checkpoint, const state::StateRange &outer,
                          std::string_view owner) {
    state::Archive::require(outer.bytes == checkpoint.bytes.size());
    for (auto &range : checkpoint.ranges) {
      range.offset += outer.offset;
      ranges.push_back(std::move(range));
    }
    constexpr auto checksum_bytes = sizeof(std::uint64_t);
    ranges.push_back({std::string(owner) + ".snapshot_checksum",
                      outer.offset + outer.bytes - checksum_bytes, checksum_bytes, checksum_bytes});
  };
  for (auto &range : archive.ranges()) {
    if (range.name == "core.snapshot.data")
      append(core, range, "core");
    else if (range.name == "gpu.snapshot.data")
      append(video, range, "gpu");
    else
      ranges.push_back(std::move(range));
  }
  return {archive.finish(), std::move(ranges)};
}

void MachineState::restore(Console &console, HardwareRenderer &renderer,
                           std::span<const std::uint8_t> data) {
  RendererState::require_memory(renderer, console.ram());
  state::Archive archive(data);
  envelope_identity(archive);
  const auto core = archive.owned_vector<std::uint8_t>({}, core_limit);
  const auto video = archive.owned_vector<std::uint8_t>({}, video_limit);
  archive.validate();
  auto core_plan = CoreState::prepare(console, core);
  auto video_plan = RendererState::prepare(renderer, video);
  RendererState::fence(renderer);
  core_plan->finish();
  video_plan->finish();
}

} // namespace cupid::n64
