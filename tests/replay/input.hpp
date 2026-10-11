#pragma once

#include "core/state/core_state.hpp"
#include <cstdint>
#include <span>
#include <vector>

namespace test::machine_replay {

enum class Mode { Interpreted, Native };
enum class ActionKind : std::uint8_t {
  CpuBlock,
  CpuSteps,
  RspRun,
  WriteMemory,
  CpuRegister,
  CpuControl,
  CpuFloating,
  CpuInstruction,
  CpuPc,
  RspRegister,
  RspVector,
  RspLocal,
  RspIo,
  RspPc,
  Controller,
  AdvanceClocks
};

struct Action {
  ActionKind kind;
  std::uint32_t address = 0;
  std::uint64_t value = 0;
  unsigned bytes = 4;
};

struct Difference {
  std::size_t boundary;
  cupid::n64::StateDifference state;
};

struct Input {
  std::uint64_t seed = 0;
  cupid::n64::VideoRegion region = cupid::n64::VideoRegion::Ntsc;
  bool expansion = true;
  std::vector<Action> initial, actions;
  std::vector<Difference> contracts;
};

bool valid(const Action &action, bool initial);
std::vector<std::uint8_t> encode(const Input &input);
Input decode(std::span<const std::uint8_t> bytes);

} // namespace test::machine_replay
