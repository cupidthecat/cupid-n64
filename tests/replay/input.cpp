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
  const auto version = archive.literal<std::uint32_t>(2);
  Archive::require(version == 1 || version == 2);
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
  if (version == 1)
    return;
  const auto count = archive.literal(static_cast<std::uint32_t>(input.contracts.size()));
  Archive::require(count <= 1024);
  if (archive.loading())
    input.contracts.resize(count);
  for (auto &contract : input.contracts) {
    if (!archive.loading())
      Archive::require(contract.boundary <= input.actions.size() &&
                       contract.state.index <= std::numeric_limits<std::uint32_t>::max());
    contract.boundary = archive.literal(static_cast<std::uint32_t>(contract.boundary));
    auto &field = contract.state.field;
    const auto length = archive.literal(static_cast<std::uint32_t>(field.size()));
    Archive::require(length && length <= 256);
    if (archive.loading())
      field.resize(length);
    for (auto &letter : field) {
      letter = archive.literal(letter);
      Archive::require((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
                       (letter >= '0' && letter <= '9') || letter == '.' || letter == '_' ||
                       letter == '[' || letter == ']');
    }
    contract.state.index = archive.literal(static_cast<std::uint32_t>(contract.state.index));
    contract.state.expected = archive.literal(contract.state.expected);
    contract.state.actual = archive.literal(contract.state.actual);
    contract.state.bytes = archive.literal(contract.state.bytes);
    Archive::require(contract.boundary <= input.actions.size() && contract.state.bytes &&
                     contract.state.bytes <= 8 && contract.state.expected != contract.state.actual);
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
