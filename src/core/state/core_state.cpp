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

void CoreState::restore(Console &console, std::span<const std::uint8_t> data) {
  const auto frequency = console.audio_.frequency_;
  state::Archive archive(data);
  visit(archive, console);
  archive.finish();
  if (console.audio_.frequency_ != frequency && console.audio_.rate_)
    console.audio_.rate_(console.audio_.frequency_);
}

void CoreState::visit(state::Archive &a, Console &c) {
  a.identity<std::uint64_t>(0x4554415453445043ull);
  a.identity<std::uint32_t>(1);
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
  visit(a, c.random_);
  a.array(c.ri_.registers_);
  a.field(c.ri_.current_loaded_);
  visit(a, c.ram_);
  a.bounded(c.mi_.lines_, 0u, 63u);
  a.bounded(c.mi_.masks_, 0u, 63u);
  a.fields(c.mi_.repeat_length_, c.mi_.repeat_, c.mi_.ebus_, c.mi_.register_select_, c.mi_.frozen_);
  visit(a, c.events_);
  visit(a, c.cic_);
  visit(a, c.pif_, c);
  visit(a, c.si_);
  visit(a, c.pi_);
  visit(a, c.disk_);
  visit(a, c.rsp_);
  visit(a, c.rdp_);
  visit(a, c.vi_);
  visit(a, c.audio_);
  memory_view(a, c.rom_, c.rom_.data_);
  a.fixed_vector(c.isviewer_.ram_);
  a.field(c.isviewer_.offset_);
  a.fixed_vector(c.eeprom_.data_);
  a.field(c.eeprom_.busy_);
  a.array(c.rtc_.data_);
  a.fields(c.rtc_.status_, c.rtc_.write_lock_);
  a.identity(c.rtc_.present_);
  a.fixed_vector(c.sram_.data_);
  memory_view(a, c.sram_, c.sram_.data_);
  a.fixed_vector(c.flash_.data_);
  a.array(c.flash_.page_);
  a.bounded(c.flash_.mode_, FlashRam::Mode::Array, FlashRam::Mode::Page);
  a.bounded(c.flash_.erase_, FlashRam::Erase::None, FlashRam::Erase::Sector);
  a.bounded(c.flash_.busy_, FlashRam::Busy::None, FlashRam::Busy::Program);
  a.fields(c.flash_.status_, c.flash_.sector_, c.flash_.offset_, c.flash_.command_high_,
           c.flash_.stale_value_, c.flash_.burst_, c.flash_.pending_command_,
           c.flash_.pending_count_, c.flash_.command_valid_, c.flash_.open_bus_, c.flash_.stale_);
  for (auto &pad : c.controllers_)
    visit(a, pad);
  for (auto &mouse : c.mice_)
    visit(a, mouse);
  for (auto &pad : c.gamecube_controllers_)
    visit(a, pad);
  visit(a, c.cpu_);
  a.fields(c.synchronized_clock_, c.clock_target_, c.frozen_);
  if (c.arcade_)
    visit(a, *c.arcade_);
  a.defer([&c] {
    c.cpu_.compiler_->reset();
    c.rsp_.compiler_->reset();
    c.ram_.instructions_.invalidate_all();
  });
}

void CoreState::visit(state::Archive &a, RandomGenerator &r) {
  a.field(r.state_);
  const auto increment = a.field(r.increment_);
  state::Archive::require(increment & 1);
}

void CoreState::visit(state::Archive &a, EventQueue &q) {
  a.field(q.clock_);
  a.bounded(q.size_, 0u, static_cast<unsigned>(q.heap_.size()));
  for (auto &entry : q.heap_) {
    a.field(entry.clock);
    a.bounded(entry.event, Event::PeripheralRead, Event::DiskMotor);
    a.field(entry.valid);
  }
}

void CoreState::visit(state::Archive &a, Cpu &c) {
  a.array(c.state_.gpr);
  a.array(c.state_.fpr);
  a.fields(c.state_.fcr31, c.state_.hi, c.state_.lo, c.state_.pc, c.state_.clocks);
  a.array(c.control_);
  for (auto &entry : c.tlb_) {
    a.fields(entry.hi, entry.mask);
    a.array(entry.lo);
  }
  for (auto &lookup : c.tlb_lookup_) {
    std::uint32_t index = 32;
    for (std::uint32_t n = 0; n < c.tlb_.size(); ++n)
      if (lookup.entry == &c.tlb_[n])
        index = n;
    state::Archive::require(!lookup.entry || index < 32);
    index = a.literal(index);
    state::Archive::require(index <= 32);
    a.defer([&c, &lookup, index] { lookup.entry = index < 32 ? &c.tlb_[index] : nullptr; });
    a.field(lookup.frequency);
  }
  for (auto *cache : {&c.icache_, &c.dcache_})
    for (auto &line : *cache) {
      a.array(line.words);
      a.fields(line.tag, line.valid, line.dirty);
    }
  a.fields(c.instruction_cache_generation_, c.control_latch_, c.cop2_latch_, c.count_ticks_,
           c.count_clock_, c.pipeline_pc_, c.next_pc_);
  visit(a, c.entropy_);
  a.fields(c.delay_slot_, c.next_delay_slot_, c.block_exit_, c.next_block_exit_, c.llbit_,
           c.nmi_pending_, c.native_memory_order_);
}

void CoreState::visit(state::Archive &a, Rsp &r) {
  a.array(r.state_.gpr);
  for (auto &vector : r.state_.vectors)
    a.array(vector.lanes);
  a.array(r.state_.accumulator.low.lanes);
  a.array(r.state_.accumulator.middle.lanes);
  a.array(r.state_.accumulator.high.lanes);
  a.fields(r.state_.carry_low, r.state_.carry_high, r.state_.compare_low, r.state_.compare_high,
           r.state_.extension, r.state_.divide_input, r.state_.divide_output,
           r.state_.divide_double);
  a.fields(r.status_.semaphore, r.status_.halted, r.status_.broken, r.status_.io_full,
           r.status_.single_step, r.status_.interrupt_on_break, r.status_.signals);
  for (auto &stage : r.pipeline_.previous)
    a.fields(stage.gpr, stage.vector, stage.load);
  auto &op = r.pipeline_.current;
  a.fields(op.flags, op.read_gpr, op.write_gpr, op.read_vector, op.write_vector, op.read_control,
           op.write_control, op.fake_vector, r.pipeline_.clocks, r.pipeline_.single_issue);
  a.array(r.memory_);
  a.fields(r.pc_, r.pipeline_pc_, r.next_pc_, r.clock_, r.delay_slot_, r.next_delay_slot_,
           r.dma_clock_, r.busy_read_, r.busy_write_, r.full_read_, r.full_write_);
  for (auto *dma : {&r.pending_, &r.current_}) {
    const auto local = a.bounded(dma->local_address, 0u, 0x1fffu);
    const auto dram = a.bounded(dma->dram_address, 0u, 0xffffffu);
    const auto length = a.bounded(dma->length, 0u, 0xfffu);
    const auto skip = a.bounded(dma->skip, 0u, 0xfffu);
    state::Archive::require(((local | dram | length | skip) & 7) == 0);
    a.bounded(dma->count, 0u, 0xffu);
  }
}

void CoreState::visit(state::Archive &a, Rdram &r) {
  a.identity(r.size());
  a.span(r.data_);
  a.fixed_vector(r.hidden_);
  a.span(r.hidden_view_);
  for (auto &chip : r.chips_) {
    a.fields(chip.present, chip.enabled, chip.auto_current, chip.device_id, chip.write_delay,
             chip.current, chip.internal_current, chip.low_current, chip.high_current, chip.row);
    a.array(chip.registers);
  }
  a.field(r.identity_);
}

void CoreState::memory_view(state::Archive &a, PeripheralMemory &m,
                            std::span<std::uint8_t> backing) {
  std::uint64_t start = 0;
  if (!m.view_.empty()) {
    const auto address = reinterpret_cast<std::uintptr_t>(m.view_.data());
    const auto base = reinterpret_cast<std::uintptr_t>(backing.data());
    state::Archive::require(address >= base && address - base <= backing.size());
    start = address - base;
  }
  start = a.literal(start);
  const auto size = a.literal(static_cast<std::uint64_t>(m.view_.size()));
  state::Archive::require(start <= backing.size() && size <= backing.size() - start);
  state::Archive::require(((start | size) & 1) == 0);
  a.defer([&m, backing, start, size] {
    m.view_ = backing.subspan(static_cast<std::size_t>(start), static_cast<std::size_t>(size));
  });
  a.fields(m.offset_, m.writable_, m.minimum_.latency, m.minimum_.pulse_width, m.minimum_.release);
}

} // namespace cupid::n64
