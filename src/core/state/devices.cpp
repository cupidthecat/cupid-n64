#include "core/state/archive.hpp"
#include "core/state/core_state.hpp"

namespace cupid::n64 {

void CoreState::visit(state::Archive &a, VideoInterface &v) {
  a.label("vi.region", sizeof(v.region_));
  a.identity(v.region_);
  a.label("vi.registers", sizeof(v.registers_[0]));
  a.array(v.registers_);
  a.label("vi.counter", sizeof(v.counter_));
  a.field(v.counter_);
  a.label("vi.leap_counter", sizeof(v.leap_counter_));
  a.field(v.leap_counter_);
  a.label("vi.field", sizeof(v.field_));
  a.field(v.field_);
  a.label("vi.fraction", sizeof(v.fraction_));
  a.field(v.fraction_);
  a.label("vi.clock", sizeof(v.clock_));
  a.field(v.clock_);
  a.label("vi.frames", sizeof(v.frames_));
  a.field(v.frames_);
  a.label("vi.software_frame.width", sizeof(v.software_frame_.width));
  const auto width = a.bounded(v.software_frame_.width, 0u, 640u);
  a.label("vi.software_frame.height", sizeof(v.software_frame_.height));
  const auto height = a.bounded(v.software_frame_.height, 0u, 576u);
  a.label("vi.software_frame.rgba");
  const auto bytes = a.vector(v.software_frame_.rgba, 640 * 576 * 4);
  state::Archive::require(bytes == std::uint64_t(width) * height * 4);
}

void CoreState::visit(state::Archive &a, AudioInterface &v) {
  a.label("audio.region", sizeof(v.region_));
  a.identity(v.region_);
  a.label("audio.state.addresses", sizeof(v.state_.addresses[0]));
  a.array(v.state_.addresses);
  a.label("audio.state.lengths", sizeof(v.state_.lengths[0]));
  a.array(v.state_.lengths);
  a.label("audio.state.count", sizeof(v.state_.count));
  a.bounded(v.state_.count, 0u, 2u);
  a.label("audio.state.enable", sizeof(v.state_.enable));
  a.field(v.state_.enable);
  a.label("audio.state.carry", sizeof(v.state_.carry));
  a.field(v.state_.carry);
  a.label("audio.state.dac_rate", sizeof(v.state_.dac_rate));
  a.field(v.state_.dac_rate);
  a.label("audio.state.bit_rate", sizeof(v.state_.bit_rate));
  a.field(v.state_.bit_rate);
  a.label("audio.output.left", sizeof(v.output_.left));
  a.field(v.output_.left);
  a.label("audio.output.right", sizeof(v.output_.right));
  a.field(v.output_.right);
  a.label("audio.clock", sizeof(v.clock_));
  a.field(v.clock_);
  a.label("audio.frequency", sizeof(v.frequency_));
  a.bounded(v.frequency_, 1u, 50000000u);
  a.label("audio.precision", sizeof(v.precision_));
  a.bounded(v.precision_, 1u, 16u);
  a.label("audio.period", sizeof(v.period_));
  a.bounded(v.period_, 1u, 187500000u);
  a.label("audio.decay", sizeof(v.decay_));
  a.field(v.decay_);
}

void CoreState::visit(state::Archive &a, PeripheralInterface &p) {
  a.label("pi.devices", 4);
  a.identity(static_cast<std::uint32_t>(p.devices_.size()));
  for (const auto &device : p.devices_)
    a.identity(device.priority);
  a.label("pi.selected", sizeof(p.selected_));
  a.bounded(p.selected_, -1, static_cast<int>(p.devices_.size()) - 1);
  a.label("pi.timing.latency", sizeof(p.timing_.latency));
  a.field(p.timing_.latency);
  a.label("pi.timing.pulse_width", sizeof(p.timing_.pulse_width));
  a.field(p.timing_.pulse_width);
  a.label("pi.timing.release", sizeof(p.timing_.release));
  a.field(p.timing_.release);
  for (auto *domain : {&p.domain1_, &p.domain2_}) {
    a.label(domain == &p.domain1_ ? "pi.domain1.latency" : "pi.domain2.latency",
            sizeof(domain->latency));
    a.field(domain->latency);
    a.label(domain == &p.domain1_ ? "pi.domain1.pulse_width" : "pi.domain2.pulse_width",
            sizeof(domain->pulse_width));
    a.field(domain->pulse_width);
    a.label(domain == &p.domain1_ ? "pi.domain1.release" : "pi.domain2.release",
            sizeof(domain->release));
    a.field(domain->release);
    a.label(domain == &p.domain1_ ? "pi.domain1.page_size" : "pi.domain2.page_size",
            sizeof(domain->page_size));
    a.field(domain->page_size);
  }
  a.label("pi.dram_address", sizeof(p.dram_address_));
  a.field(p.dram_address_);
  a.label("pi.bus_address", sizeof(p.bus_address_));
  a.field(p.bus_address_);
  a.label("pi.read_length", sizeof(p.read_length_));
  a.field(p.read_length_);
  a.label("pi.write_length", sizeof(p.write_length_));
  a.field(p.write_length_);
  a.label("pi.latch", sizeof(p.latch_));
  a.field(p.latch_);
  a.label("pi.dma_busy", sizeof(p.dma_busy_));
  a.field(p.dma_busy_);
  a.label("pi.io_busy", sizeof(p.io_busy_));
  a.field(p.io_busy_);
  a.label("pi.error", sizeof(p.error_));
  a.field(p.error_);
  a.label("pi.interrupt", sizeof(p.interrupt_));
  a.field(p.interrupt_);
}

void CoreState::visit(state::Archive &a, SerialInterface &s) {
  a.label("si.dram_address", sizeof(s.dram_address_));
  a.field(s.dram_address_);
  a.label("si.read_address", sizeof(s.read_address_));
  a.field(s.read_address_);
  a.label("si.write_address", sizeof(s.write_address_));
  a.field(s.write_address_);
  a.label("si.latch", sizeof(s.latch_));
  a.field(s.latch_);
  a.label("si.pch_state", sizeof(s.pch_state_));
  a.field(s.pch_state_);
  a.label("si.dma_state", sizeof(s.dma_state_));
  a.field(s.dma_state_);
  a.label("si.dma_busy", sizeof(s.dma_busy_));
  a.field(s.dma_busy_);
  a.label("si.io_busy", sizeof(s.io_busy_));
  a.field(s.io_busy_);
  a.label("si.interrupt", sizeof(s.interrupt_));
  a.field(s.interrupt_);
}

void CoreState::visit(state::Archive &a, Rdp &r) {
  auto &s = r.command_;
  a.label("rdp.start", sizeof(s.start));
  a.field(s.start);
  a.label("rdp.end", sizeof(s.end));
  a.field(s.end);
  a.label("rdp.current", sizeof(s.current));
  a.field(s.current);
  a.label("rdp.command.clock", sizeof(s.clock));
  a.field(s.clock);
  a.label("rdp.buffer_busy", sizeof(s.buffer_busy));
  a.field(s.buffer_busy);
  a.label("rdp.pipe_busy", sizeof(s.pipe_busy));
  a.field(s.pipe_busy);
  a.label("rdp.tmem_busy", sizeof(s.tmem_busy));
  a.field(s.tmem_busy);
  a.label("rdp.source", sizeof(s.source));
  a.field(s.source);
  a.label("rdp.freeze", sizeof(s.freeze));
  a.field(s.freeze);
  a.label("rdp.crashed", sizeof(s.crashed));
  a.field(s.crashed);
  a.label("rdp.flush", sizeof(s.flush));
  a.field(s.flush);
  a.label("rdp.start_valid", sizeof(s.start_valid));
  a.field(s.start_valid);
  a.label("rdp.end_valid", sizeof(s.end_valid));
  a.field(s.end_valid);
  a.label("rdp.start_clock", sizeof(s.start_clock));
  a.field(s.start_clock);
  a.label("rdp.ready", sizeof(s.ready));
  a.field(s.ready);
  a.label("rdp.clock", sizeof(r.clock_));
  a.field(r.clock_);
  a.label("rdp.buffer", sizeof(r.buffer_[0]));
  a.array(r.buffer_);
  a.label("rdp.queue_size", sizeof(r.queue_size_));
  const auto size = a.bounded(r.queue_size_, 0u, static_cast<unsigned>(r.buffer_.size()));
  a.label("rdp.queue_offset", sizeof(r.queue_offset_));
  a.bounded(r.queue_offset_, 0u, size);
  a.label("rdp.test.check", sizeof(r.test_.check));
  a.field(r.test_.check);
  a.label("rdp.test.go", sizeof(r.test_.go));
  a.field(r.test_.go);
  a.label("rdp.test.done", sizeof(r.test_.done));
  a.field(r.test_.done);
  a.label("rdp.test.enable", sizeof(r.test_.enable));
  a.field(r.test_.enable);
  a.label("rdp.test.fail", sizeof(r.test_.fail));
  a.field(r.test_.fail);
  a.label("rdp.test.address", sizeof(r.test_.address));
  a.bounded(r.test_.address, std::uint8_t(0), std::uint8_t(127));
  a.label("rdp.test.data", sizeof(r.test_.data[0]));
  a.array(r.test_.data);
}

void CoreState::visit(state::Archive &a, Cic &c) {
  a.label("cic.fifo", sizeof(c.fifo_[0]));
  a.array(c.fifo_);
  a.label("cic.head", sizeof(c.head_));
  a.bounded(c.head_, 0u, 127u);
  a.label("cic.count", sizeof(c.count_));
  a.field(c.count_);
  a.label("cic.seed", sizeof(c.seed_));
  a.field(c.seed_);
  a.label("cic.checksum", sizeof(c.checksum_));
  a.field(c.checksum_);
  a.label("cic.pal", sizeof(c.pal_));
  a.field(c.pal_);
  a.label("cic.disk", sizeof(c.disk_));
  a.field(c.disk_);
  a.label("cic.real_challenge", sizeof(c.real_challenge_));
  a.field(c.real_challenge_);
  a.label("cic.state", sizeof(c.state_));
  a.bounded(c.state_, Cic::State::Region, Cic::State::Dead);
}

void CoreState::visit(state::Archive &a, Pif &p, Console &c) {
  a.label("pif.ram", sizeof(p.ram_[0]));
  a.array(p.ram_);
  a.label("pif.os_info", sizeof(p.os_info_[0]));
  a.array(p.os_info_);
  a.label("pif.cpu_checksum", sizeof(p.cpu_checksum_[0]));
  a.array(p.cpu_checksum_);
  a.label("pif.cic_checksum", sizeof(p.cic_checksum_[0]));
  a.array(p.cic_checksum_);
  a.label("pif.joy_address", sizeof(p.joy_address_[0]));
  a.array(p.joy_address_);
  a.label("pif.joy_skip", sizeof(p.joy_skip_[0]));
  a.array(p.joy_skip_);
  a.label("pif.joy_reset", sizeof(p.joy_reset_[0]));
  a.array(p.joy_reset_);
  std::array<JoybusDevice *, 16> devices{};
  devices[1] = &c.cartridge_joybus_;
  for (unsigned port = 0; port < 4; ++port) {
    devices[2 + port] = &c.controllers_[port];
    devices[6 + port] = &c.mice_[port];
    devices[10 + port] = &c.gamecube_controllers_[port];
  }
  if (c.arcade_)
    for (unsigned port = 0; port < 2; ++port)
      devices[14 + port] = &c.arcade_->controller(port);
  a.label("pif.devices", 4);
  for (auto &device : p.devices_) {
    const auto found = std::find(devices.begin(), devices.end(), device);
    state::Archive::require(found != devices.end());
    const auto index = a.literal(static_cast<std::uint32_t>(found - devices.begin()));
    state::Archive::require(index < devices.size() && (!index || devices[index]));
    a.defer([&device, target = devices[index]] { device = target; });
  }
  a.label("pif.clock", sizeof(p.clock_));
  a.field(p.clock_);
  a.label("pif.timeout", sizeof(p.timeout_));
  a.field(p.timeout_);
  a.label("pif.locked", sizeof(p.locked_));
  a.field(p.locked_);
  a.label("pif.reset_enabled", sizeof(p.reset_enabled_));
  a.field(p.reset_enabled_);
  a.label("pif.state", sizeof(p.state_));
  a.bounded(p.state_, Pif::State::Init, Pif::State::Error);
}

void CoreState::visit(state::Archive &a, DiskDrive &d) {
  a.label("disk.image");
  a.fixed_vector(d.disk_, "disk.image");
  a.label("disk.errors");
  a.fixed_vector(d.errors_, "disk.errors");
  a.label("disk.clock.data", sizeof(d.clock_.data_[0]));
  a.array(d.clock_.data_);
  a.label("disk.correction", sizeof(d.correction_[0]));
  a.array(d.correction_);
  a.label("disk.sector", sizeof(d.sector_[0]));
  a.array(d.sector_);
  a.label("disk.sequence", sizeof(d.sequence_[0]));
  a.array(d.sequence_);
  a.label("disk.asic", sizeof(d.asic_));
  a.field(d.asic_);
  a.label("disk.mecha_irq", sizeof(d.mecha_irq_));
  a.field(d.mecha_irq_);
  a.label("disk.block_irq", sizeof(d.block_irq_));
  a.field(d.block_irq_);
  a.label("disk.seeking", sizeof(d.seeking_));
  a.field(d.seeking_);
  a.label("disk.block_reset", sizeof(d.block_reset_));
  a.field(d.block_reset_);
  a.label("disk.reading", sizeof(d.reading_));
  a.field(d.reading_);
  a.label("disk.standby_disabled", sizeof(d.standby_disabled_));
  a.field(d.standby_disabled_);
  a.label("disk.sleep_disabled", sizeof(d.sleep_disabled_));
  a.field(d.sleep_disabled_);
  a.label("disk.data", sizeof(d.data_));
  a.field(d.data_);
  a.label("disk.track", sizeof(d.track_));
  a.field(d.track_);
  a.label("disk.status", sizeof(d.status_));
  a.field(d.status_);
  a.label("disk.block_status", sizeof(d.block_status_));
  a.field(d.block_status_);
  a.label("disk.current_sector", sizeof(d.current_sector_));
  a.field(d.current_sector_);
  a.label("disk.sector_bytes", sizeof(d.sector_bytes_));
  a.field(d.sector_bytes_);
  a.label("disk.transfer_bytes", sizeof(d.transfer_bytes_));
  a.field(d.transfer_bytes_);
  a.label("disk.sector_block", sizeof(d.sector_block_));
  a.field(d.sector_block_);
  a.label("disk.disk_type", sizeof(d.disk_type_));
  a.field(d.disk_type_);
  a.label("disk.drive_errors", sizeof(d.drive_errors_));
  a.field(d.drive_errors_);
  std::uint32_t view = 0;
  const std::array<std::span<std::uint8_t>, 4> views{d.ipl_, d.correction_, d.sector_, d.sequence_};
  for (unsigned index = 0; !d.view_.empty() && index < views.size(); ++index) {
    const auto candidate = views[index];
    if (candidate.data() == d.view_.data() && candidate.size() == d.view_.size())
      view = index + 1;
    if (view)
      break;
  }
  state::Archive::require(d.view_.empty() || view);
  a.label("disk.bus_view", 4);
  view = a.literal(view);
  state::Archive::require(view <= 4 && (view != 1 || !d.ipl_.empty()));
  a.defer([&d, view] {
    switch (view) {
    case 0:
      d.view_ = {};
      break;
    case 1:
      d.view_ = d.ipl_;
      break;
    case 2:
      d.view_ = d.correction_;
      break;
    case 3:
      d.view_ = d.sector_;
      break;
    case 4:
      d.view_ = d.sequence_;
      break;
    }
  });
  a.label("disk.offset", sizeof(d.offset_));
  a.field(d.offset_);
  a.label("disk.writable", sizeof(d.writable_));
  a.field(d.writable_);
  a.label("disk.minimum.latency", sizeof(d.minimum_.latency));
  a.field(d.minimum_.latency);
  a.label("disk.minimum.pulse_width", sizeof(d.minimum_.pulse_width));
  a.field(d.minimum_.pulse_width);
  a.label("disk.minimum.release", sizeof(d.minimum_.release));
  a.field(d.minimum_.release);
}

} // namespace cupid::n64
