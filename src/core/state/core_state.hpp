#pragma once

#include "core/state/archive.hpp"
#include "core/system/console.hpp"
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cupid::n64 {

class HandheldCartridge;

namespace state {
class Archive;
}

struct StateDifference {
  std::string field;
  std::size_t index = 0;
  std::uint64_t expected = 0, actual = 0;
  unsigned bytes = 0;
};

struct StateCheckpoint {
  std::vector<std::uint8_t> bytes;
  std::vector<state::StateRange> ranges;
  std::optional<StateDifference> difference(const StateCheckpoint &actual) const;
};

// Call between execution intervals. The caller owns GPU fencing and renderer state.
class CoreState {
public:
  static std::vector<std::uint8_t> capture(Console &console);
  static StateCheckpoint checkpoint(Console &console);
  static void restore(Console &console, std::span<const std::uint8_t> data);
  static std::unique_ptr<state::Archive> prepare(Console &console,
                                                 std::span<const std::uint8_t> data);

private:
  static void visit(state::Archive &archive, Console &console);
  static void visit(state::Archive &archive, Cpu &cpu);
  static void visit(state::Archive &archive, Rsp &rsp);
  static void visit(state::Archive &archive, RspCompiler &compiler);
  static void visit(state::Archive &archive, CpuCompiler &compiler, InstructionTracker &tracker);
  static void visit(state::Archive &archive, InstructionTracker &tracker);
  static void visit(state::Archive &archive, Rdram &ram);
  static void visit(state::Archive &archive, EventQueue &events);
  static void visit(state::Archive &archive, RandomGenerator &random);
  static void visit(state::Archive &archive, VideoInterface &video);
  static void visit(state::Archive &archive, AudioInterface &audio);
  static void visit(state::Archive &archive, PeripheralInterface &pi);
  static void visit(state::Archive &archive, SerialInterface &si);
  static void visit(state::Archive &archive, Rdp &rdp);
  static void visit(state::Archive &archive, Cic &cic);
  static void visit(state::Archive &archive, Pif &pif, Console &console);
  static void visit(state::Archive &archive, Gamepad &pad, unsigned port);
  static void visit(state::Archive &archive, Mouse &mouse, unsigned port);
  static void visit(state::Archive &archive, GameCubePad &pad, unsigned port);
  static void visit(state::Archive &archive, TransferPak &pak, unsigned port);
  static void visit(state::Archive &archive, BioSensor &sensor, unsigned port);
  static void visit(state::Archive &archive, HandheldCartridge &cartridge, unsigned port);
  static void visit(state::Archive &archive, DiskDrive &drive);
  static void visit(state::Archive &archive, Aleck64 &arcade);
  static void memory_view(state::Archive &archive, PeripheralMemory &memory,
                          std::span<std::uint8_t> backing, std::string_view name);
};

} // namespace cupid::n64
