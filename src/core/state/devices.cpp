#include "core/state/archive.hpp"
#include "core/state/core_state.hpp"

namespace cupid::n64 {

void CoreState::visit(state::Archive &a, VideoInterface &v) {
  a.identity(v.region_);
  a.array(v.registers_);
  a.fields(v.counter_, v.leap_counter_, v.field_, v.fraction_, v.clock_, v.frames_);
  const auto width = a.bounded(v.software_frame_.width, 0u, 640u);
  const auto height = a.bounded(v.software_frame_.height, 0u, 576u);
  const auto bytes = a.vector(v.software_frame_.rgba, 640 * 576 * 4);
  state::Archive::require(bytes == std::uint64_t(width) * height * 4);
}

void CoreState::visit(state::Archive &a, AudioInterface &v) {
  a.identity(v.region_);
  a.array(v.state_.addresses);
  a.array(v.state_.lengths);
  a.bounded(v.state_.count, 0u, 2u);
  a.fields(v.state_.enable, v.state_.carry, v.state_.dac_rate, v.state_.bit_rate, v.output_.left,
           v.output_.right, v.clock_);
  a.bounded(v.frequency_, 1u, 50000000u);
  a.bounded(v.precision_, 1u, 16u);
  a.bounded(v.period_, 1u, 187500000u);
  a.field(v.decay_);
}

void CoreState::visit(state::Archive &a, PeripheralInterface &p) {
  a.identity(static_cast<std::uint32_t>(p.devices_.size()));
  for (const auto &device : p.devices_)
    a.identity(device.priority);
  a.bounded(p.selected_, -1, static_cast<int>(p.devices_.size()) - 1);
  a.fields(p.timing_.latency, p.timing_.pulse_width, p.timing_.release);
  for (auto *domain : {&p.domain1_, &p.domain2_})
    a.fields(domain->latency, domain->pulse_width, domain->release, domain->page_size);
  a.fields(p.dram_address_, p.bus_address_, p.read_length_, p.write_length_, p.latch_, p.dma_busy_,
           p.io_busy_, p.error_, p.interrupt_);
}

void CoreState::visit(state::Archive &a, SerialInterface &s) {
  a.fields(s.dram_address_, s.read_address_, s.write_address_, s.latch_, s.pch_state_, s.dma_state_,
           s.dma_busy_, s.io_busy_, s.interrupt_);
}

void CoreState::visit(state::Archive &a, Rdp &r) {
  auto &s = r.command_;
  a.fields(s.start, s.end, s.current, s.clock, s.buffer_busy, s.pipe_busy, s.tmem_busy, s.source,
           s.freeze, s.crashed, s.flush, s.start_valid, s.end_valid, s.start_clock, s.ready,
           r.clock_);
  a.array(r.buffer_);
  const auto size = a.bounded(r.queue_size_, 0u, static_cast<unsigned>(r.buffer_.size()));
  a.bounded(r.queue_offset_, 0u, size);
  a.fields(r.test_.check, r.test_.go, r.test_.done, r.test_.enable, r.test_.fail);
  a.bounded(r.test_.address, std::uint8_t(0), std::uint8_t(127));
  a.array(r.test_.data);
}

void CoreState::visit(state::Archive &a, Cic &c) {
  a.array(c.fifo_);
  a.bounded(c.head_, 0u, 127u);
  a.fields(c.count_, c.seed_, c.checksum_, c.pal_, c.disk_, c.real_challenge_);
  a.bounded(c.state_, Cic::State::Region, Cic::State::Dead);
}

void CoreState::visit(state::Archive &a, Pif &p, Console &c) {
  a.array(p.ram_);
  a.array(p.os_info_);
  a.array(p.cpu_checksum_);
  a.array(p.cic_checksum_);
  a.array(p.joy_address_);
  a.array(p.joy_skip_);
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
  for (auto &device : p.devices_) {
    const auto found = std::find(devices.begin(), devices.end(), device);
    state::Archive::require(found != devices.end());
    const auto index = a.literal(static_cast<std::uint32_t>(found - devices.begin()));
    state::Archive::require(index < devices.size() && (!index || devices[index]));
    a.defer([&device, target = devices[index]] { device = target; });
  }
  a.fields(p.clock_, p.timeout_, p.locked_, p.reset_enabled_);
  a.bounded(p.state_, Pif::State::Init, Pif::State::Error);
}

void CoreState::visit(state::Archive &a, DiskDrive &d) {
  a.fixed_vector(d.disk_);
  a.fixed_vector(d.errors_);
  a.array(d.clock_.data_);
  a.array(d.correction_);
  a.array(d.sector_);
  a.array(d.sequence_);
  a.fields(d.asic_, d.mecha_irq_, d.block_irq_, d.seeking_, d.block_reset_, d.reading_,
           d.standby_disabled_, d.sleep_disabled_, d.data_, d.track_, d.status_, d.block_status_,
           d.current_sector_, d.sector_bytes_, d.transfer_bytes_, d.sector_block_, d.disk_type_,
           d.drive_errors_);
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
  a.fields(d.offset_, d.writable_, d.minimum_.latency, d.minimum_.pulse_width, d.minimum_.release);
}

} // namespace cupid::n64
