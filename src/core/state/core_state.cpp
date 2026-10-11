#include "core/state/core_state.hpp"
#include "core/cpu/recompiler.hpp"
#include "core/rsp/recompiler.hpp"
#include "core/state/archive.hpp"

namespace cupid::n64 {

std::vector<std::uint8_t> CoreState::capture(Console &console) {
  state::Archive archive;
  visit(archive, console);
  return archive.finish();
}

StateCheckpoint CoreState::checkpoint(Console &console) {
  state::Archive archive(true);
  visit(archive, console);
  auto ranges = archive.ranges();
  auto bytes = archive.finish();
  return {std::move(bytes), std::move(ranges)};
}

void CoreState::restore(Console &console, std::span<const std::uint8_t> data) {
  auto archive = prepare(console, data);
  archive->finish();
}

std::unique_ptr<state::Archive> CoreState::prepare(Console &console,
                                                   std::span<const std::uint8_t> data) {
  const auto frequency = console.audio_.frequency_;
  auto archive = std::make_unique<state::Archive>(data);
  visit(*archive, console);
  archive->validate();
  archive->defer([&console, frequency] {
    if (console.audio_.frequency_ != frequency && console.audio_.rate_)
      console.audio_.rate_(console.audio_.frequency_);
  });
  return archive;
}

void CoreState::visit(state::Archive &a, Console &c) {
  a.label("machine.identity");
  a.identity<std::uint64_t>(0x4554415453445043ull);
  a.identity<std::uint32_t>(3);
  a.identity(c.config_.region);
  a.identity(c.config_.expansion);
  a.identity(c.config_.cic);
  a.identity(c.config_.eeprom_size);
  a.identity(c.config_.sram_size);
  a.identity(c.config_.flash_model.has_value());
  if (c.config_.flash_model)
    a.identity(*c.config_.flash_model);
  a.identity(c.config_.rtc_present);
  a.identity(c.config_.disk_drive);
  a.identity(c.config_.arcade_profile);
  a.bytes_identity(c.rom_.data_);
  a.bytes_identity(c.pif_.rom_);
  a.bytes_identity(c.disk_.ipl_);
  a.label("machine.random", 8);
  visit(a, c.random_);
  a.label("ri.registers", 4);
  a.array(c.ri_.registers_);
  a.label("ri.current_loaded", 1);
  a.field(c.ri_.current_loaded_);
  visit(a, c.ram_);
  a.label("mi.lines", sizeof(c.mi_.lines_));
  a.bounded(c.mi_.lines_, 0u, 63u);
  a.label("mi.masks", sizeof(c.mi_.masks_));
  a.bounded(c.mi_.masks_, 0u, 63u);
  a.label("mi.repeat_length", sizeof(c.mi_.repeat_length_));
  a.field(c.mi_.repeat_length_);
  a.label("mi.repeat", sizeof(c.mi_.repeat_));
  a.field(c.mi_.repeat_);
  a.label("mi.ebus", sizeof(c.mi_.ebus_));
  a.field(c.mi_.ebus_);
  a.label("mi.register_select", sizeof(c.mi_.register_select_));
  a.field(c.mi_.register_select_);
  a.label("mi.frozen", sizeof(c.mi_.frozen_));
  a.field(c.mi_.frozen_);
  a.label("events");
  visit(a, c.events_);
  a.label("cic");
  visit(a, c.cic_);
  a.label("pif");
  visit(a, c.pif_, c);
  a.label("si");
  visit(a, c.si_);
  a.label("pi");
  visit(a, c.pi_);
  a.label("disk");
  visit(a, c.disk_);
  visit(a, c.rsp_);
  a.label("rdp");
  visit(a, c.rdp_);
  a.label("vi");
  visit(a, c.vi_);
  a.label("audio");
  visit(a, c.audio_);
  a.label("cartridge.bus");
  memory_view(a, c.rom_, c.rom_.data_, "rom.bus");
  a.label("isviewer");
  a.fixed_vector(c.isviewer_.ram_, "isviewer.ram");
  a.label("isviewer.offset", sizeof(c.isviewer_.offset_));
  a.field(c.isviewer_.offset_);
  a.label("eeprom");
  a.fixed_vector(c.eeprom_.data_, "eeprom");
  a.label("eeprom.busy", sizeof(c.eeprom_.busy_));
  a.field(c.eeprom_.busy_);
  a.label("cartridge.rtc");
  a.label("cartridge.rtc.data", sizeof(c.rtc_.data_[0]));
  a.array(c.rtc_.data_);
  a.label("cartridge.rtc.status", sizeof(c.rtc_.status_));
  a.field(c.rtc_.status_);
  a.label("cartridge.rtc.write_lock", sizeof(c.rtc_.write_lock_));
  a.field(c.rtc_.write_lock_);
  a.label("cartridge.rtc.present", sizeof(c.rtc_.present_));
  a.identity(c.rtc_.present_);
  a.label("sram");
  a.fixed_vector(c.sram_.data_, "sram");
  memory_view(a, c.sram_, c.sram_.data_, "sram.bus");
  a.label("flash");
  a.fixed_vector(c.flash_.data_, "flash");
  a.label("flash.page", sizeof(c.flash_.page_[0]));
  a.array(c.flash_.page_);
  a.label("flash.mode", sizeof(c.flash_.mode_));
  a.bounded(c.flash_.mode_, FlashRam::Mode::Array, FlashRam::Mode::Page);
  a.label("flash.erase", sizeof(c.flash_.erase_));
  a.bounded(c.flash_.erase_, FlashRam::Erase::None, FlashRam::Erase::Sector);
  a.label("flash.busy", sizeof(c.flash_.busy_));
  a.bounded(c.flash_.busy_, FlashRam::Busy::None, FlashRam::Busy::Program);
  a.label("flash.status", sizeof(c.flash_.status_));
  a.field(c.flash_.status_);
  a.label("flash.sector", sizeof(c.flash_.sector_));
  a.field(c.flash_.sector_);
  a.label("flash.offset", sizeof(c.flash_.offset_));
  a.field(c.flash_.offset_);
  a.label("flash.command_high", sizeof(c.flash_.command_high_));
  a.field(c.flash_.command_high_);
  a.label("flash.stale_value", sizeof(c.flash_.stale_value_));
  a.field(c.flash_.stale_value_);
  a.label("flash.burst", sizeof(c.flash_.burst_));
  a.field(c.flash_.burst_);
  a.label("flash.pending_command", sizeof(c.flash_.pending_command_));
  a.field(c.flash_.pending_command_);
  a.label("flash.pending_count", sizeof(c.flash_.pending_count_));
  a.field(c.flash_.pending_count_);
  a.label("flash.command_valid", sizeof(c.flash_.command_valid_));
  a.field(c.flash_.command_valid_);
  a.label("flash.open_bus", sizeof(c.flash_.open_bus_));
  a.field(c.flash_.open_bus_);
  a.label("flash.stale", sizeof(c.flash_.stale_));
  a.field(c.flash_.stale_);
  a.label("controllers");
  for (unsigned port = 0; port < c.controllers_.size(); ++port)
    visit(a, c.controllers_[port], port);
  a.label("mice");
  for (unsigned port = 0; port < c.mice_.size(); ++port)
    visit(a, c.mice_[port], port);
  a.label("gamecube");
  for (unsigned port = 0; port < c.gamecube_controllers_.size(); ++port)
    visit(a, c.gamecube_controllers_[port], port);
  visit(a, c.cpu_);
  a.label("machine.synchronized_clock", sizeof(c.synchronized_clock_));
  a.field(c.synchronized_clock_);
  a.label("machine.clock_target", sizeof(c.clock_target_));
  a.field(c.clock_target_);
  a.label("machine.frozen", sizeof(c.frozen_));
  a.field(c.frozen_);
  a.label("arcade");
  if (c.arcade_)
    visit(a, *c.arcade_);
  a.label("rsp.execution_history");
  visit(a, *c.rsp_.compiler_);
  a.label("ram.instruction_history");
  visit(a, c.ram_.instructions_);
  a.label("cpu.execution_history");
  visit(a, *c.cpu_.compiler_, c.ram_.instructions_);
}

void CoreState::visit(state::Archive &a, RandomGenerator &r) {
  a.field(r.state_);
  const auto increment = a.field(r.increment_);
  state::Archive::require(increment & 1);
}

void CoreState::visit(state::Archive &a, EventQueue &q) {
  a.label("events.clock", 4);
  a.field(q.clock_);
  a.label("events.size", 4);
  a.bounded(q.size_, 0u, static_cast<unsigned>(q.heap_.size()));
  for (unsigned index = 0; index < q.heap_.size(); ++index) {
    auto &entry = q.heap_[index];
    a.indexed_label("events.heap", index, "clock", 4);
    a.field(entry.clock);
    a.indexed_label("events.heap", index, "event", 4);
    a.bounded(entry.event, Event::PeripheralRead, Event::DiskMotor);
    a.indexed_label("events.heap", index, "valid");
    a.field(entry.valid);
  }
}

void CoreState::visit(state::Archive &a, Cpu &c) {
  a.label("cpu.gpr", 8);
  a.array(c.state_.gpr);
  a.label("cpu.fpr", 8);
  a.array(c.state_.fpr);
  a.label("cpu.fcr31", 4);
  a.field(c.state_.fcr31);
  a.label("cpu.hi", 8);
  a.field(c.state_.hi);
  a.label("cpu.lo", 8);
  a.field(c.state_.lo);
  a.label("cpu.pc", 8);
  a.field(c.state_.pc);
  a.label("cpu.clocks", 8);
  a.field(c.state_.clocks);
  a.label("cpu.cp0", 8);
  a.array(c.control_);
  for (unsigned index = 0; index < c.tlb_.size(); ++index) {
    auto &entry = c.tlb_[index];
    a.indexed_label("cpu.tlb", index, "hi", 8);
    a.field(entry.hi);
    a.indexed_label("cpu.tlb", index, "mask", 4);
    a.field(entry.mask);
    a.indexed_label("cpu.tlb", index, "lo", 4);
    a.array(entry.lo);
  }
  for (unsigned slot = 0; slot < c.tlb_lookup_.size(); ++slot) {
    auto &lookup = c.tlb_lookup_[slot];
    std::uint32_t index = 32;
    for (std::uint32_t n = 0; n < c.tlb_.size(); ++n)
      if (lookup.entry == &c.tlb_[n])
        index = n;
    state::Archive::require(!lookup.entry || index < 32);
    a.indexed_label("cpu.tlb_lookup", slot, "entry", 4);
    index = a.literal(index);
    state::Archive::require(index <= 32);
    a.defer([&c, &lookup, index] { lookup.entry = index < 32 ? &c.tlb_[index] : nullptr; });
    a.indexed_label("cpu.tlb_lookup", slot, "frequency", sizeof(lookup.frequency));
    a.field(lookup.frequency);
  }
  for (auto *cache : {&c.icache_, &c.dcache_})
    for (unsigned index = 0; index < cache->size(); ++index) {
      auto &line = (*cache)[index];
      const auto name = cache == &c.icache_ ? "cpu.icache" : "cpu.dcache";
      a.indexed_label(name, index, "words", 4);
      a.array(line.words);
      a.indexed_label(name, index, "tag", 4);
      a.field(line.tag);
      a.indexed_label(name, index, "valid");
      a.field(line.valid);
      a.indexed_label(name, index, "dirty");
      a.field(line.dirty);
    }
  a.label("cpu.instruction_cache_generation", 8);
  a.field(c.instruction_cache_generation_);
  a.label("cpu.control_latch", 8);
  a.field(c.control_latch_);
  a.label("cpu.cop2_latch", 8);
  a.field(c.cop2_latch_);
  a.label("cpu.count_ticks", 8);
  a.field(c.count_ticks_);
  a.label("cpu.count_clock", 8);
  a.field(c.count_clock_);
  a.label("cpu.pipeline_pc", 8);
  a.field(c.pipeline_pc_);
  a.label("cpu.next_pc", 8);
  a.field(c.next_pc_);
  a.label("cpu.entropy", 8);
  visit(a, c.entropy_);
  a.label("cpu.delay_slot");
  a.field(c.delay_slot_);
  a.label("cpu.next_delay_slot");
  a.field(c.next_delay_slot_);
  a.label("cpu.block_exit");
  a.field(c.block_exit_);
  a.label("cpu.next_block_exit");
  a.field(c.next_block_exit_);
  a.label("cpu.llbit");
  a.field(c.llbit_);
  a.label("cpu.nmi_pending");
  a.field(c.nmi_pending_);
  a.label("cpu.native_memory_order");
  a.field(c.native_memory_order_);
}

void CoreState::visit(state::Archive &a, Rsp &r) {
  a.label("rsp.gpr", 4);
  a.array(r.state_.gpr);
  a.label("rsp.vector", 2);
  for (auto &vector : r.state_.vectors)
    a.array(vector.lanes);
  a.label("rsp.accumulator.low", 2);
  a.array(r.state_.accumulator.low.lanes);
  a.label("rsp.accumulator.middle", 2);
  a.array(r.state_.accumulator.middle.lanes);
  a.label("rsp.accumulator.high", 2);
  a.array(r.state_.accumulator.high.lanes);
  a.label("rsp.carry_low", 1);
  a.field(r.state_.carry_low);
  a.label("rsp.carry_high", 1);
  a.field(r.state_.carry_high);
  a.label("rsp.compare_low", 1);
  a.field(r.state_.compare_low);
  a.label("rsp.compare_high", 1);
  a.field(r.state_.compare_high);
  a.label("rsp.extension", 1);
  a.field(r.state_.extension);
  a.label("rsp.divide_input", 2);
  a.field(r.state_.divide_input);
  a.label("rsp.divide_output", 2);
  a.field(r.state_.divide_output);
  a.label("rsp.divide_double", 1);
  a.field(r.state_.divide_double);
  a.label("rsp.status.semaphore", sizeof(r.status_.semaphore));
  a.field(r.status_.semaphore);
  a.label("rsp.status.halted", sizeof(r.status_.halted));
  a.field(r.status_.halted);
  a.label("rsp.status.broken", sizeof(r.status_.broken));
  a.field(r.status_.broken);
  a.label("rsp.status.io_full", sizeof(r.status_.io_full));
  a.field(r.status_.io_full);
  a.label("rsp.status.single_step", sizeof(r.status_.single_step));
  a.field(r.status_.single_step);
  a.label("rsp.status.interrupt_on_break", sizeof(r.status_.interrupt_on_break));
  a.field(r.status_.interrupt_on_break);
  a.label("rsp.status.signals", sizeof(r.status_.signals));
  a.field(r.status_.signals);
  for (unsigned index = 0; index < r.pipeline_.previous.size(); ++index) {
    auto &stage = r.pipeline_.previous[index];
    a.indexed_label("rsp.pipeline", index, "gpr", 4);
    a.field(stage.gpr);
    a.indexed_label("rsp.pipeline", index, "vector", 4);
    a.field(stage.vector);
    a.indexed_label("rsp.pipeline", index, "load");
    a.field(stage.load);
  }
  auto &op = r.pipeline_.current;
  a.label("rsp.pipeline.current.flags", 4);
  a.field(op.flags);
  a.label("rsp.pipeline.current.read_gpr", 4);
  a.field(op.read_gpr);
  a.label("rsp.pipeline.current.write_gpr", 4);
  a.field(op.write_gpr);
  a.label("rsp.pipeline.current.read_vector", 4);
  a.field(op.read_vector);
  a.label("rsp.pipeline.current.write_vector", 4);
  a.field(op.write_vector);
  a.label("rsp.pipeline.current.read_control", 4);
  a.field(op.read_control);
  a.label("rsp.pipeline.current.write_control", 4);
  a.field(op.write_control);
  a.label("rsp.pipeline.current.fake_vector", 4);
  a.field(op.fake_vector);
  a.label("rsp.pipeline.clocks", 4);
  a.field(r.pipeline_.clocks);
  a.label("rsp.pipeline.single_issue");
  a.field(r.pipeline_.single_issue);
  a.label("rsp.local_memory");
  a.array(r.memory_);
  a.label("rsp.pc", 4);
  a.field(r.pc_);
  a.label("rsp.pipeline_pc", 4);
  a.field(r.pipeline_pc_);
  a.label("rsp.next_pc", 4);
  a.field(r.next_pc_);
  a.label("rsp.clock", 8);
  a.field(r.clock_);
  a.label("rsp.delay_slot", 1);
  a.field(r.delay_slot_);
  a.label("rsp.next_delay_slot", 1);
  a.field(r.next_delay_slot_);
  a.label("rsp.dma_clock", 8);
  a.field(r.dma_clock_);
  a.label("rsp.busy_read", 1);
  a.field(r.busy_read_);
  a.label("rsp.busy_write", 1);
  a.field(r.busy_write_);
  a.label("rsp.full_read", 1);
  a.field(r.full_read_);
  a.label("rsp.full_write", 1);
  a.field(r.full_write_);
  for (auto *dma : {&r.pending_, &r.current_}) {
    a.label(dma == &r.pending_ ? "rsp.dma.pending.local_address" : "rsp.dma.current.local_address",
            sizeof(dma->local_address));
    const auto local = a.bounded(dma->local_address, 0u, 0x1fffu);
    a.label(dma == &r.pending_ ? "rsp.dma.pending.dram_address" : "rsp.dma.current.dram_address",
            sizeof(dma->dram_address));
    const auto dram = a.bounded(dma->dram_address, 0u, 0xffffffu);
    a.label(dma == &r.pending_ ? "rsp.dma.pending.length" : "rsp.dma.current.length",
            sizeof(dma->length));
    const auto length = a.bounded(dma->length, 0u, 0xfffu);
    a.label(dma == &r.pending_ ? "rsp.dma.pending.skip" : "rsp.dma.current.skip",
            sizeof(dma->skip));
    const auto skip = a.bounded(dma->skip, 0u, 0xfffu);
    state::Archive::require(((local | dram | length | skip) & 7) == 0);
    a.label(dma == &r.pending_ ? "rsp.dma.pending.count" : "rsp.dma.current.count",
            sizeof(dma->count));
    a.bounded(dma->count, 0u, 0xffu);
  }
}

void CoreState::visit(state::Archive &a, Rdram &r) {
  a.label("ram.size", 4);
  a.identity(r.size());
  a.label("ram.words", 4);
  a.span(r.data_);
  a.label("ram.hidden");
  a.fixed_vector(r.hidden_, "ram.hidden");
  a.label("ram.hidden_view");
  a.span(r.hidden_view_);
  for (unsigned index = 0; index < r.chips_.size(); ++index) {
    auto &chip = r.chips_[index];
    a.indexed_label("ram.chips", index, "present", sizeof(chip.present));
    a.field(chip.present);
    a.indexed_label("ram.chips", index, "enabled", sizeof(chip.enabled));
    a.field(chip.enabled);
    a.indexed_label("ram.chips", index, "auto_current", sizeof(chip.auto_current));
    a.field(chip.auto_current);
    a.indexed_label("ram.chips", index, "device_id", sizeof(chip.device_id));
    a.field(chip.device_id);
    a.indexed_label("ram.chips", index, "write_delay", sizeof(chip.write_delay));
    a.field(chip.write_delay);
    a.indexed_label("ram.chips", index, "current", sizeof(chip.current));
    a.field(chip.current);
    a.indexed_label("ram.chips", index, "internal_current", sizeof(chip.internal_current));
    a.field(chip.internal_current);
    a.indexed_label("ram.chips", index, "low_current", sizeof(chip.low_current));
    a.field(chip.low_current);
    a.indexed_label("ram.chips", index, "high_current", sizeof(chip.high_current));
    a.field(chip.high_current);
    a.indexed_label("ram.chips", index, "row", sizeof(chip.row));
    a.field(chip.row);
    a.indexed_label("ram.chips", index, "registers", sizeof(chip.registers[0]));
    a.array(chip.registers);
  }
  a.label("ram.identity");
  a.field(r.identity_);
}

void CoreState::memory_view(state::Archive &a, PeripheralMemory &m, std::span<std::uint8_t> backing,
                            std::string_view name) {
  std::uint64_t start = 0;
  if (!m.view_.empty()) {
    const auto address = reinterpret_cast<std::uintptr_t>(m.view_.data());
    const auto base = reinterpret_cast<std::uintptr_t>(backing.data());
    state::Archive::require(address >= base && address - base <= backing.size());
    start = address - base;
  }
  a.member_label(name, "start", 8);
  start = a.literal(start);
  a.member_label(name, "size", 8);
  const auto size = a.literal(static_cast<std::uint64_t>(m.view_.size()));
  state::Archive::require(start <= backing.size() && size <= backing.size() - start);
  state::Archive::require(((start | size) & 1) == 0);
  a.defer([&m, backing, start, size] {
    m.view_ = backing.subspan(static_cast<std::size_t>(start), static_cast<std::size_t>(size));
  });
  a.member_label(name, "offset", sizeof(m.offset_));
  a.field(m.offset_);
  a.member_label(name, "writable", sizeof(m.writable_));
  a.field(m.writable_);
  a.member_label(name, "minimum_.latency", sizeof(m.minimum_.latency));
  a.field(m.minimum_.latency);
  a.member_label(name, "minimum_.pulse_width", sizeof(m.minimum_.pulse_width));
  a.field(m.minimum_.pulse_width);
  a.member_label(name, "minimum_.release", sizeof(m.minimum_.release));
  a.field(m.minimum_.release);
}

} // namespace cupid::n64
