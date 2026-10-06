#include "core/devices/pif/pif.hpp"
#include <algorithm>
#include <utility>

namespace cupid::n64 {

Pif::Pif(Cic &cic, Rdram &ram) : cic_(cic), dram_(ram) {
  power();
}

bool Pif::load_rom(std::span<const std::uint8_t> data) {
  if (data.size() != rom_.size())
    return false;
  std::copy(data.begin(), data.end(), rom_.begin());
  return true;
}

void Pif::connect_reset(std::function<void()> callback) {
  reset_ = std::move(callback);
}

void Pif::attach(unsigned channel, JoybusDevice *device) {
  devices_[channel] = device;
}

void Pif::power() {
  ram_ = {};
  os_info_ = {};
  cpu_checksum_ = cic_checksum_ = {};
  joy_address_ = {};
  joy_skip_ = joy_reset_ = {};
  clock_ = timeout_ = 0;
  locked_ = reset_enabled_ = false;
  state_ = State::Init;
}

void Pif::advance(std::uint32_t clocks) {
  elapse(clocks);
  run();
}

void Pif::run() {
  while (clock_ < 0)
    tick();
}

std::uint32_t Pif::read_internal(std::uint32_t address) const {
  address &= 0x7fc;
  const auto data =
      address < 0x7c0 ? std::span<const std::uint8_t>(rom_) : std::span<const std::uint8_t>(ram_);
  if (address < 0x7c0 && locked_)
    return 0;
  const auto offset = address < 0x7c0 ? address : address & 63;
  return (std::uint32_t(data[offset]) << 24) | (std::uint32_t(data[offset + 1]) << 16) |
         (std::uint32_t(data[offset + 2]) << 8) | data[offset + 3];
}

void Pif::write_internal(std::uint32_t address, std::uint32_t value) {
  address &= 0x7fc;
  if (address < 0x7c0)
    return;
  for (unsigned n = 0; n < 4; ++n)
    ram_[(address & 63) + n] = static_cast<std::uint8_t>(value >> ((3 - n) * 8));
}

std::uint32_t Pif::read_word(std::uint32_t address) {
  access(true, false);
  return read_internal(address);
}

void Pif::write_word(std::uint32_t address, std::uint32_t value) {
  write_internal(address, value);
  access(false, false);
  tick();
}

void Pif::dma_read(std::uint32_t address, std::uint32_t dram_address) {
  access(true, true);
  for (unsigned offset = 0; offset < 64; offset += 4)
    dram_.write(dram_address + offset, 4, read_internal(address + offset));
}

void Pif::dma_write(std::uint32_t address, std::uint32_t dram_address) {
  for (unsigned offset = 0; offset < 64; offset += 4)
    write_internal(address + offset,
                   static_cast<std::uint32_t>(dram_.read(dram_address + offset, 4)));
  access(false, true);
  tick();
}

void Pif::descramble(std::span<std::uint8_t> data) {
  for (unsigned n = static_cast<unsigned>(data.size()) - 1; n; --n)
    data[n] = (data[n] - data[n - 1] - 1) & 15;
}

void Pif::swap_secrets() {
  for (unsigned n = 0; n < os_info_.size(); ++n)
    std::swap(os_info_[n], ram_[0x25 + n]);
  for (unsigned n = 0; n < cpu_checksum_.size(); ++n)
    std::swap(cpu_checksum_[n], ram_[0x32 + n]);
}

void Pif::tick() {
  constexpr int clocks = 10240 * 8;
  clock_ += clocks;
  if (timeout_ > 0)
    timeout_ -= clocks;
  if (state_ == State::Run)
    return;
  if (state_ == State::Init) {
    const auto hello = cic_.read_nibble();
    if ((hello & 3) != 1) {
      state_ = State::Error;
      return;
    }
    std::array<std::uint8_t, 6> data{};
    for (auto &value : data)
      value = static_cast<std::uint8_t>(cic_.read_nibble());
    descramble(data);
    descramble(data);
    for (unsigned n = 0; n < 3; ++n)
      os_info_[n] = (data[n * 2] << 4) | data[n * 2 + 1];
    os_info_[0] = (os_info_[0] & 0xf0) | 4 | (hello & 8);
    ram_[63] = 0;
    swap_secrets();
    state_ = State::Lockout;
    return;
  }
  if (state_ == State::Lockout && (ram_[63] & 0x10)) {
    locked_ = true;
    joy_init();
    state_ = State::GetChecksum;
    return;
  }
  if (state_ == State::GetChecksum && (ram_[63] & 0x20)) {
    swap_secrets();
    ram_[63] |= 0x80;
    state_ = State::CheckChecksum;
    return;
  }
  if (state_ == State::CheckChecksum && (ram_[63] & 0x40)) {
    std::array<std::uint8_t, 16> data{};
    for (auto &value : data)
      value = static_cast<std::uint8_t>(cic_.read_nibble());
    for (unsigned n = 0; n < 4; ++n)
      descramble(data);
    for (unsigned n = 0; n < 6; ++n)
      cic_checksum_[n] = (data[n * 2 + 4] << 4) | data[n * 2 + 5];
    os_info_[0] |= 2;
    if (cpu_checksum_ != cic_checksum_) {
      state_ = State::Error;
      return;
    }
    cpu_checksum_ = {};
    state_ = State::Terminate;
    timeout_ = 6 * 187500000;
    return;
  }
  if (state_ == State::Terminate && (ram_[63] & 8)) {
    ram_[63] = 0;
    reset_enabled_ = true;
    state_ = State::Run;
    return;
  }
  if (state_ == State::Terminate && timeout_ <= 0) {
    state_ = State::Error;
    return;
  }
  if (state_ == State::Error && reset_)
    reset_();
}

} // namespace cupid::n64
