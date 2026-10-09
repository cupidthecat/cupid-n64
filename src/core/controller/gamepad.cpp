#include "core/controller/gamepad.hpp"
#include "core/controller/stick/response.hpp"
#include <algorithm>
#include <array>

namespace cupid::n64 {

Gamepad::Gamepad(RandomGenerator &random) : random_(random) {}

void Gamepad::input(std::uint16_t buttons, std::int8_t x, std::int8_t y) {
  buttons_ = buttons;
  x_ = x;
  y_ = y;
}

void Gamepad::input_host(std::uint16_t buttons, std::int16_t x, std::int16_t y) {
  const auto position = stick_response(x, y);
  input(buttons, position.x, static_cast<std::int8_t>(-position.y));
}

std::uint32_t Gamepad::state() const {
  unsigned buttons = buttons_ & 0xff3f;
  if ((buttons & 0x0300) == 0x0300)
    buttons &= ~0x0300u;
  if ((buttons & 0x0c00) == 0x0c00)
    buttons &= ~0x0c00u;
  if ((buttons & 0x1030) == 0x1030)
    return ((buttons & ~0x1000u) | 0x80) << 16;
  return (buttons << 16) | (std::uint32_t(static_cast<std::uint8_t>(x_)) << 8) |
         static_cast<std::uint8_t>(y_);
}

void Gamepad::memory_pak(unsigned banks) {
  disconnect_pak();
  pak_ = Pak::Memory;
  detect_ = true;
  motor_ = false;
  bank_ = 0;
  ram_.assign(std::clamp(banks, 1u, 62u) * 0x8000, 0);
  format();
}

void Gamepad::rumble_pak() {
  disconnect_pak();
  pak_ = Pak::Rumble;
}

void Gamepad::transfer_pak(std::shared_ptr<TransferCartridge> cartridge) {
  disconnect_pak();
  pak_ = Pak::Transfer;
  transfer_.connect(std::move(cartridge));
}

void Gamepad::bio_sensor(BioSensor::HostClock clock) {
  disconnect_pak();
  pak_ = Pak::BioSensor;
  sensor_.connect(std::move(clock));
}

void Gamepad::disconnect_pak() {
  pak_ = Pak::None;
  detect_ = true;
  motor_ = false;
  ram_.clear();
  transfer_.connect();
  sensor_.disconnect();
}

JoybusStatus Gamepad::communicate(std::span<const std::uint8_t> input,
                                  std::span<std::uint8_t> output) {
  if (input.empty())
    return {};
  std::array<std::uint8_t, 64> response{};
  unsigned response_length = 0;
  JoybusStatus status;
  const auto command = input[0];
  if (command == 0 || command == 255) {
    response[0] = 5;
    response[2] = pak_ == Pak::None ? 2 : detect_ ? 3 : 1;
    detect_ = false;
    status.valid = true;
    response_length = 3;
  }
  if (command == 1) {
    const auto value = state();
    for (unsigned n = 0; n < 4; ++n)
      response[n] = static_cast<std::uint8_t>(value >> ((3 - n) * 8));
    status = {true, output.size() > 4};
    response_length = 4;
  }
  if (command == 2 && input.size() >= 3 && !output.empty()) {
    unsigned address = ((unsigned(input[1]) << 8) | input[2]) & ~31u;
    const bool unavailable =
        pak_ == Pak::None || detect_ ||
        address_crc(static_cast<std::uint16_t>(address)) != (unsigned(input[2]) & 31);
    if (!unavailable) {
      if (pak_ == Pak::BioSensor)
        sensor_.update();
      for (unsigned n = 0; n < std::min(output.size(), std::size_t(32)); ++n) {
        if (pak_ == Pak::Memory)
          response[n] = address <= 0x7fff ? ram_[bank_ * 0x8000 + address] : 0;
        if (pak_ == Pak::Rumble)
          response[n] = address <= 0x7fff ? 0 : address <= 0x8fff ? 0x80 : motor_ ? 255 : 0;
        if (pak_ == Pak::Transfer)
          response[n] = transfer_.read(static_cast<std::uint16_t>(address));
        if (pak_ == Pak::BioSensor)
          response[n] = sensor_.read(static_cast<std::uint16_t>(address));
        ++address;
      }
    }
    status.valid = true;
    response_length = 33;
    if (output.size() >= 33) {
      response[32] = data_crc(std::span<const std::uint8_t, 32>(response.data(), 32));
      if (unavailable)
        response[32] ^= 255;
    }
  }
  if (command == 3 && input.size() >= 4 && !output.empty()) {
    unsigned address = ((unsigned(input[1]) << 8) | input[2]) & ~31u;
    const auto length = std::min(input.size() - 3, std::size_t(32));
    const bool unavailable =
        pak_ == Pak::None || detect_ ||
        address_crc(static_cast<std::uint16_t>(address)) != (unsigned(input[2]) & 31);
    if (!unavailable) {
      if (pak_ == Pak::Memory) {
        if (address == 0x8000) {
          if (input[3] < ram_.size() / 0x8000)
            bank_ = input[3];
        } else {
          for (unsigned n = 0; n < length; ++n) {
            if (address <= 0x7fff)
              ram_[bank_ * 0x8000 + address] = input[n + 3];
            ++address;
          }
        }
      }
      if (pak_ == Pak::Rumble && address >= 0xc000)
        motor_ = input[3] & 1;
      if (pak_ == Pak::Transfer)
        for (unsigned n = 0; n < length; ++n)
          transfer_.write(static_cast<std::uint16_t>(address + n), input[n + 3]);
    }
    if (length == 32)
      response[0] = data_crc(std::span<const std::uint8_t, 32>(input.data() + 3, 32));
    if (unavailable)
      response[0] ^= 255;
    status.valid = true;
    response_length = 1;
  }
  std::copy_n(response.begin(), std::min(output.size(), std::size_t(response_length)),
              output.begin());
  return status;
}

} // namespace cupid::n64
