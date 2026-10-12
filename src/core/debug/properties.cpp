#include "core/debug/inspection.hpp"
#include "core/system/console.hpp"
#include <array>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace cupid::n64 {
namespace {

class Properties {
public:
  std::vector<DeviceProperty> values;
  void text(std::string name, std::string value) {
    values.push_back({std::move(name), std::move(value)});
  }
  void hex(std::string name, std::uint64_t value, unsigned width = 8) {
    std::ostringstream formatted;
    formatted.imbue(std::locale::classic());
    formatted << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(width)
              << value;
    text(std::move(name), formatted.str());
  }
  template <typename Value> void number(std::string name, Value value) {
    text(std::move(name), std::to_string(value));
  }
  void flag(std::string name, bool value) {
    text(std::move(name), value ? "Yes" : "No");
  }
  template <std::size_t Size, typename Read>
  void registers(const std::array<const char *, Size> &names, Read read) {
    for (unsigned n = 0; n < Size; ++n)
      hex(names[n], read(n * 4));
  }
};

constexpr std::array<DeviceInfo, 14> device_names{
    {{DeviceId::Console, "Console"},
     {DeviceId::Cpu, "CPU"},
     {DeviceId::Rsp, "RSP"},
     {DeviceId::Rdram, "RDRAM"},
     {DeviceId::Mi, "MI interrupts"},
     {DeviceId::Vi, "VI video"},
     {DeviceId::Ai, "AI audio"},
     {DeviceId::Pi, "PI peripheral DMA"},
     {DeviceId::Si, "SI serial DMA"},
     {DeviceId::Ri, "RI memory interface"},
     {DeviceId::Rdp, "RDP"},
     {DeviceId::Pif, "PIF"},
     {DeviceId::Disk, "64DD"},
     {DeviceId::Controllers, "Controller connections"}}};

} // namespace

std::span<const DeviceInfo> Inspection::devices() {
  return device_names;
}

std::vector<DeviceProperty> Inspection::properties(const Console &console, DeviceId device) {
  Properties result;
  switch (device) {
  case DeviceId::Console: {
    constexpr std::array cic{"5101", "6101", "6102", "7101", "7102", "6103", "7103", "6105",
                             "7105", "6106", "7106", "8303", "8401", "5167", "DDUS"};
    result.number("CPU clocks", console.cpu_.state().clocks);
    result.number("Last synchronized CPU clock", console.synchronized_clock_);
    result.flag("Bus frozen", console.frozen_);
    result.text("Video region", console.config_.region == VideoRegion::Pal ? "PAL" : "NTSC");
    result.flag("Expansion Pak", console.config_.expansion);
    result.text("CIC", cic.at(static_cast<unsigned>(console.boot_cic())));
    result.flag("Boot checksum verification", console.config_.boot_verification.checksum);
    result.flag("Boot region verification", console.config_.boot_verification.region);
    result.flag("64DD configured", console.config_.disk_drive);
    result.number("Aleck64 profile", static_cast<unsigned>(console.config_.arcade_profile));
    result.number("Cartridge ROM bytes", console.rom_.data().size());
    result.number("SRAM bytes", console.sram_.data().size());
    result.number("EEPROM bytes", console.eeprom_.data().size());
    result.number("Flash bytes", console.flash_.data().size());
    result.flag("Cartridge RTC", console.config_.rtc_present);
    break;
  }
  case DeviceId::Cpu: {
    const auto &cpu = console.cpu_;
    const auto &state = cpu.state_;
    result.hex("PC", state.pc, 16);
    result.hex("HI", state.hi, 16);
    result.hex("LO", state.lo, 16);
    result.number("Clocks", state.clocks);
    result.flag("Branch delay slot", cpu.delay_slot_);
    result.flag("Load-linked reservation", cpu.llbit_);
    result.flag("NMI pending", cpu.nmi_pending_);
    for (unsigned n = 0; n < 32; ++n) {
      result.hex("GPR " + std::to_string(n), state.gpr[n], 16);
      result.hex("FPR " + std::to_string(n), state.fpr[n], 16);
    }
    result.hex("FCR31", state.fcr31);
    constexpr std::array<const char *, 32> control{
        "Index", "Random",   "EntryLo0", "EntryLo1", "Context", "PageMask",    "Wired",
        nullptr, "BadVAddr", "Count",    "EntryHi",  "Compare", "Status",      "Cause",
        "EPC",   "PRId",     "Config",   "LLAddr",   "WatchLo", "WatchHi",     "XContext",
        nullptr, nullptr,    nullptr,    nullptr,    nullptr,   "ParityError", "CacheError",
        "TagLo", "TagHi",    "ErrorEPC", nullptr};
    for (unsigned n = 0; n < control.size(); ++n) {
      if (!control[n])
        continue;
      if (n == Random) {
        result.text("CP0 Random", "Selected on guest read");
        continue;
      }
      const auto value = n == Count ? cpu.timer_count() : cpu.control_[n];
      result.hex(std::string("CP0 ") + control[n], value, 16);
    }
    for (unsigned n = 0; n < cpu.tlb_.size(); ++n) {
      const auto prefix = "TLB " + std::to_string(n) + " ";
      result.hex(prefix + "EntryHi", cpu.tlb_[n].hi, 16);
      result.hex(prefix + "EntryLo0", cpu.tlb_[n].lo[0]);
      result.hex(prefix + "EntryLo1", cpu.tlb_[n].lo[1]);
      result.hex(prefix + "PageMask", cpu.tlb_[n].mask);
    }
    break;
  }
  case DeviceId::Rsp: {
    const auto &rsp = console.rsp_;
    result.hex("PC", rsp.pc_);
    result.number("Clock offset", rsp.clock_);
    result.flag("Halted", rsp.status_.halted);
    result.flag("Broke", rsp.status_.broken);
    result.flag("Semaphore", rsp.status_.semaphore);
    result.flag("Single step", rsp.status_.single_step);
    result.flag("Interrupt on break", rsp.status_.interrupt_on_break);
    result.hex("Signals", rsp.status_.signals, 2);
    result.flag("DMA read busy", rsp.busy_read_);
    result.flag("DMA write busy", rsp.busy_write_);
    result.flag("DMA read full", rsp.full_read_);
    result.flag("DMA write full", rsp.full_write_);
    result.number("DMA clocks remaining", rsp.dma_clock_);
    const auto dma = [&](const auto &registers, const std::string &prefix) {
      result.hex(prefix + "local address", registers.local_address);
      result.hex(prefix + "RDRAM address", registers.dram_address);
      result.number(prefix + "length", registers.length);
      result.number(prefix + "count", registers.count);
      result.number(prefix + "skip", registers.skip);
    };
    dma(rsp.current_, "Current DMA ");
    dma(rsp.pending_, "Pending DMA ");
    for (unsigned n = 0; n < 32; ++n) {
      result.hex("GPR " + std::to_string(n), rsp.state_.gpr[n]);
      for (unsigned lane = 0; lane < 8; ++lane)
        result.hex("Vector " + std::to_string(n) + " lane " + std::to_string(lane),
                   rsp.state_.vectors[n].lanes[lane], 4);
    }
    for (unsigned lane = 0; lane < 8; ++lane)
      result.hex("Accumulator lane " + std::to_string(lane), rsp.state_.accumulator.get(lane), 12);
    result.hex("Carry low", rsp.state_.carry_low, 2);
    result.hex("Carry high", rsp.state_.carry_high, 2);
    result.hex("Compare low", rsp.state_.compare_low, 2);
    result.hex("Compare high", rsp.state_.compare_high, 2);
    result.hex("Extension", rsp.state_.extension, 2);
    result.hex("Divide input", rsp.state_.divide_input, 4);
    result.hex("Divide output", rsp.state_.divide_output, 4);
    result.flag("Divide double", rsp.state_.divide_double);
    break;
  }
  case DeviceId::Rdram: {
    const auto &ram = console.ram_;
    result.number("Backing bytes", ram.size());
    result.flag("Identity mapping", ram.identity_);
    constexpr std::array names{"Device type",  "Device ID",        "Delay",
                               "Mode",         "Refresh interval", "Refresh row",
                               "RAS interval", "Minimum interval", "Address select",
                               "Manufacturer"};
    for (unsigned n = 0; n < ram.chips_.size(); ++n) {
      const auto &chip = ram.chips_[n];
      const auto prefix = "Chip " + std::to_string(n) + " ";
      result.flag(prefix + "present", chip.present);
      result.flag(prefix + "enabled", chip.enabled);
      result.number(prefix + "device ID", chip.device_id);
      result.number(prefix + "current", chip.current);
      result.number(prefix + "internal current", chip.internal_current);
      result.flag(prefix + "automatic current", chip.auto_current);
      result.hex(prefix + "row", chip.row);
      for (unsigned reg = 0; reg < names.size(); ++reg)
        result.hex(prefix + names[reg] + " (stored)", chip.registers[reg]);
    }
    break;
  }
  case DeviceId::Mi: {
    result.registers(std::array{"MI_INIT_MODE", "MI_VERSION", "MI_INTR", "MI_INTR_MASK"},
                     [&](unsigned offset) { return console.mi_.read_word(offset); });
    constexpr std::array names{"SP", "SI", "AI", "VI", "PI", "DP"};
    const auto pending = console.mi_.read_word(8), enabled = console.mi_.read_word(12);
    for (unsigned n = 0; n < names.size(); ++n) {
      result.flag(std::string(names[n]) + " interrupt pending", pending & (1u << n));
      result.flag(std::string(names[n]) + " interrupt enabled", enabled & (1u << n));
    }
    break;
  }
  case DeviceId::Vi:
    result.registers(std::array{"VI_CONTROL", "VI_DRAM_ADDRESS", "VI_H_WIDTH", "VI_V_INTR",
                                "VI_V_CURRENT_LINE", "VI_TIMING", "VI_V_SYNC", "VI_H_SYNC",
                                "VI_H_SYNC_LEAP", "VI_H_VIDEO", "VI_V_VIDEO", "VI_V_BURST",
                                "VI_X_SCALE", "VI_Y_SCALE"},
                     [&](unsigned offset) { return console.vi_.peek_word(offset); });
    result.flag("Field", console.vi_.field());
    result.number("Frames", console.vi_.frames());
    result.number("Clock offset", console.vi_.clocks());
    result.number("Clock fraction", console.vi_.fraction());
    break;
  case DeviceId::Ai: {
    const auto &audio = console.audio_;
    const auto &state = audio.state();
    const std::array<std::uint32_t, 6> registers{state.addresses[0],     state.lengths[0],
                                                 unsigned(state.enable), audio.status(),
                                                 state.dac_rate,         state.bit_rate};
    result.registers(std::array{"AI_DRAM_ADDRESS", "AI_LENGTH", "AI_CONTROL", "AI_STATUS",
                                "AI_DACRATE", "AI_BITRATE"},
                     [&](unsigned offset) { return registers[offset / 4]; });
    for (unsigned n = 0; n < 2; ++n) {
      result.hex("DMA " + std::to_string(n) + " address", console.audio_.state().addresses[n]);
      result.number("DMA " + std::to_string(n) + " remaining bytes",
                    console.audio_.state().lengths[n]);
    }
    result.number("Queued DMA buffers", console.audio_.state().count);
    result.number("Frequency Hz", console.audio_.frequency());
    result.number("Clock offset", console.audio_.clocks());
    break;
  }
  case DeviceId::Pi:
    result.registers(std::array{"PI_DRAM_ADDRESS", "PI_PBUS_ADDRESS", "PI_READ_LENGTH",
                                "PI_WRITE_LENGTH", "PI_STATUS", "PI_BSD_DOM1_LAT",
                                "PI_BSD_DOM1_PWD", "PI_BSD_DOM1_PGS", "PI_BSD_DOM1_RLS",
                                "PI_BSD_DOM2_LAT", "PI_BSD_DOM2_PWD", "PI_BSD_DOM2_PGS",
                                "PI_BSD_DOM2_RLS"},
                     [&](unsigned offset) { return console.pi_.read_io(offset); });
    break;
  case DeviceId::Si:
    result.registers(std::array{"SI_DRAM_ADDRESS", "SI_PIF_ADDRESS_READ64B",
                                "SI_INT_ADDRESS_WRITE64B", "SI_RESERVED", "SI_PIF_ADDRESS_WRITE64B",
                                "SI_INT_ADDRESS_READ64B", "SI_STATUS"},
                     [&](unsigned offset) { return console.si_.peek_io(offset); });
    break;
  case DeviceId::Ri:
    result.registers(std::array{"RI_MODE", "RI_CONFIG", "RI_CURRENT_LOAD", "RI_SELECT",
                                "RI_REFRESH", "RI_LATENCY", "RI_ERROR", "RI_BANK_STATUS"},
                     [&](unsigned offset) { return console.ri_.read_word(offset); });
    result.flag("Memory bus active", console.ri_.active());
    break;
  case DeviceId::Rdp: {
    const auto &rdp = console.rdp_;
    const auto &command = rdp.command();
    const std::array registers{command.start,     command.end,         command.current,
                               rdp.status(),      rdp.clock_counter(), command.buffer_busy,
                               command.pipe_busy, command.tmem_busy};
    result.registers(std::array{"DPC_START", "DPC_END", "DPC_CURRENT", "DPC_STATUS", "DPC_CLOCK",
                                "DPC_BUSY", "DPC_PIPE_BUSY", "DPC_TMEM_BUSY"},
                     [&](unsigned offset) { return registers[offset / 4]; });
    result.number("Clock offset", console.rdp_.clocks());
    result.number("Buffered command words", console.rdp_.buffered_words());
    result.number("Consumed command words", console.rdp_.consumed_words());
    result.flag("Crashed", console.rdp_.command().crashed);
    break;
  }
  case DeviceId::Pif: {
    constexpr std::array states{"Init",      "Lockout", "Get checksum", "Check checksum",
                                "Terminate", "Run",     "Error"};
    result.text("State", states.at(static_cast<unsigned>(console.pif_.state_)));
    result.number("Clock offset", console.pif_.clock_);
    result.number("Timeout", console.pif_.timeout_);
    result.flag("Locked", console.pif_.locked_);
    result.flag("Reset enabled", console.pif_.reset_enabled_);
    result.hex("RAM command byte", console.pif_.ram_.back(), 2);
    break;
  }
  case DeviceId::Disk: {
    const auto &disk = console.disk_;
    result.flag("Configured", console.config_.disk_drive);
    result.flag("IPL loaded", !disk.ipl_.empty());
    result.flag("Disk inserted", !disk.disk_.empty());
    result.number("Disk bytes", disk.disk_.size());
    result.hex("ASIC data", disk.data_, 4);
    result.hex("ASIC status", disk.status_, 4);
    result.hex("ASIC current track", disk.track_, 4);
    result.hex("Block status", disk.block_status_, 4);
    result.number("Current sector", disk.current_sector_);
    result.number("Sector bytes", disk.sector_bytes_);
    result.number("Transfer bytes", disk.transfer_bytes_);
    result.number("Drive errors", disk.drive_errors_);
    result.flag("Mechanical interrupt", disk.mecha_irq_);
    result.flag("Block interrupt", disk.block_irq_);
    result.flag("Seeking", disk.seeking_);
    result.flag("Reading", disk.reading_);
    break;
  }
  case DeviceId::Controllers:
    for (unsigned n = 0; n < 4; ++n) {
      const auto *connected = console.pif_.devices_[n];
      const auto prefix = "Port " + std::to_string(n + 1) + " ";
      result.text(prefix + "controller",
                  !connected                                       ? "Disconnected"
                  : connected == &console.controllers_[n]          ? "Nintendo 64 controller"
                  : connected == &console.mice_[n]                 ? "Nintendo 64 mouse"
                  : connected == &console.gamecube_controllers_[n] ? "GameCube controller"
                                                                   : "Joybus device");
      if (connected == &console.controllers_[n]) {
        result.hex(prefix + "input", console.controllers_[n].state());
        result.flag(prefix + "rumble", console.controllers_[n].rumbling());
      }
      if (connected == &console.mice_[n])
        result.hex(prefix + "input", console.mice_[n].state());
      if (connected == &console.gamecube_controllers_[n])
        result.flag(prefix + "rumble", console.gamecube_controllers_[n].rumbling());
    }
    result.flag("Cartridge Joybus device connected", console.pif_.devices_[4] != nullptr);
    break;
  default:
    throw std::runtime_error("This device is unavailable.");
  }
  return std::move(result.values);
}

} // namespace cupid::n64
