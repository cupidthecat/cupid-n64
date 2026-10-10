#pragma once

#include "core/system/console.hpp"
#include <span>
#include <vector>

namespace cupid::n64 {

class HandheldCartridge;

namespace state {
class Archive;
}

// Call between execution intervals. The caller owns GPU fencing and renderer state.
class CoreState {
public:
  static std::vector<std::uint8_t> capture(Console &console);
  static void restore(Console &console, std::span<const std::uint8_t> data);

private:
  static void visit(state::Archive &archive, Console &console);
  static void visit(state::Archive &archive, Cpu &cpu);
  static void visit(state::Archive &archive, Rsp &rsp);
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
  static void visit(state::Archive &archive, Gamepad &pad);
  static void visit(state::Archive &archive, Mouse &mouse);
  static void visit(state::Archive &archive, GameCubePad &pad);
  static void visit(state::Archive &archive, TransferPak &pak);
  static void visit(state::Archive &archive, BioSensor &sensor);
  static void visit(state::Archive &archive, HandheldCartridge &cartridge);
  static void visit(state::Archive &archive, DiskDrive &drive);
  static void visit(state::Archive &archive, Aleck64 &arcade);
  static void memory_view(state::Archive &archive, PeripheralMemory &memory,
                          std::span<std::uint8_t> backing);
};

} // namespace cupid::n64
