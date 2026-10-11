#include "input.hpp"
#include "core/state/archive.hpp"

namespace test::machine_replay {

bool valid(const Action &a, bool initial) {
  const bool width = a.bytes == 1 || a.bytes == 2 || a.bytes == 4 || a.bytes == 8;
  switch (a.kind) {
  case ActionKind::CpuBlock:
  case ActionKind::RspRun:
  case ActionKind::AdvanceClocks:
    return !initial && a.value <= 1'000'000;
  case ActionKind::CpuSteps:
    return !initial && a.value <= 10'000;
  case ActionKind::WriteMemory:
    return width && a.address < 0x20000000;
  case ActionKind::CpuRegister:
  case ActionKind::CpuControl:
  case ActionKind::CpuFloating:
  case ActionKind::RspRegister:
    return a.address < 32;
  case ActionKind::CpuInstruction:
    return a.value <= 0xffffffff;
  case ActionKind::CpuPc:
    return true;
  case ActionKind::RspVector:
    return a.address < 256 && a.value <= 0xffff;
  case ActionKind::RspLocal:
    return width && a.address <= 0x2000 - a.bytes;
  case ActionKind::RspIo:
    return a.address < 32 && !(a.address & 3) && a.value <= 0xffffffff;
  case ActionKind::RspPc:
    return a.value < 4096 && !(a.value & 3);
  case ActionKind::Controller:
    return a.address < 4 && a.value <= 0xffffffff;
  }
  return false;
}

namespace {
void visit(cupid::n64::state::Archive &archive, Input &input) {
  using cupid::n64::state::Archive;
  archive.identity<std::uint64_t>(0x59414c5045525043ull);
  archive.identity<std::uint32_t>(1);
  input.seed = archive.literal(input.seed);
  input.region = archive.literal(input.region);
  Archive::require(input.region == cupid::n64::VideoRegion::Ntsc ||
                   input.region == cupid::n64::VideoRegion::Pal);
  input.expansion = archive.literal(input.expansion);
  for (bool initial : {true, false}) {
    auto &actions = initial ? input.initial : input.actions;
    const auto size = archive.literal(static_cast<std::uint32_t>(actions.size()));
    Archive::require(size <= 1024);
    if (archive.loading())
      actions.resize(size);
    for (auto &action : actions) {
      action.kind = archive.literal(action.kind);
      action.address = archive.literal(action.address);
      action.value = archive.literal(action.value);
      action.bytes = archive.literal(action.bytes);
      Archive::require(valid(action, initial));
    }
  }
}
} // namespace

std::vector<std::uint8_t> encode(const Input &input) {
  auto owned = input;
  cupid::n64::state::Archive archive;
  visit(archive, owned);
  return archive.finish();
}

Input decode(std::span<const std::uint8_t> bytes) {
  Input input;
  cupid::n64::state::Archive archive(bytes);
  visit(archive, input);
  archive.finish();
  return input;
}

} // namespace test::machine_replay
