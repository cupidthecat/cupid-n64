#include "implementation.hpp"
#include <algorithm>

namespace cupid::n64 {

void HandheldCartridge::Implementation::serial_reset() {
  serial_select = serial_clock = false;
  serial_bits = serial_remaining = serial_busy = 0;
  serial_input = serial_output = 0;
}

std::uint8_t HandheldCartridge::Implementation::serial_read() const {
  unsigned bit = 1;
  if (serial_select) {
    if (serial_busy)
      bit = 0;
    else if (serial_remaining)
      bit = serial_output & 1;
  }
  return static_cast<std::uint8_t>(0x3c | bit | ((serial_input & 1) << 1) |
                                   (unsigned(serial_clock) << 6) | (unsigned(serial_select) << 7));
}

void HandheldCartridge::Implementation::serial_write(std::uint8_t value) {
  if (serial_select && !(value & 0x80)) {
    serial_reset();
    return;
  }
  serial_select = value & 0x80;
  if (!serial_select)
    return;
  const auto edge = !serial_clock && (value & 0x40);
  serial_clock = value & 0x40;
  if (!edge)
    return;
  const unsigned address_bits = config.eeprom_size == 128 ? 6 : config.eeprom_size <= 512 ? 8 : 10;
  const auto address_mask = (1u << address_bits) - 1;
  const unsigned header = address_bits + 3;
  const auto read_word = [&] {
    const auto offset = (serial_address * 2) & (eeprom.size() - 1);
    const unsigned word = (unsigned(eeprom[offset]) << 8) | eeprom[offset + 1];
    serial_output = serial_remaining = 0;
    for (unsigned bit = 0; bit < 16; ++bit) {
      const unsigned data = (word >> bit) & 1;
      if (!serial_remaining && !data)
        continue;
      serial_output = (serial_output << 1) | data;
      ++serial_remaining;
    }
    if (serial_remaining) {
      serial_output <<= 1;
      ++serial_remaining;
    }
  };
  const unsigned incoming = (value >> 1) & 1;
  if (serial_remaining && !incoming) {
    const auto start = (serial_input >> ((serial_bits - 1) & 31)) & 1;
    const auto opcode = (serial_input >> ((serial_bits - 3) & 31)) & 3;
    if (start && opcode == 2) {
      serial_output >>= 1;
      if (!--serial_remaining) {
        const auto next = (serial_input + 1) & address_mask;
        serial_input = (serial_input & ~std::uint64_t(address_mask)) | next;
        serial_address =
            static_cast<unsigned>((serial_input >> ((serial_bits - header) & 31)) & address_mask);
        read_word();
      }
    }
    return;
  }
  serial_output = serial_remaining = 0;
  if (!serial_bits && !incoming)
    return;
  serial_input = static_cast<std::uint32_t>((serial_input << 1) | incoming);
  ++serial_bits;
  if (!((serial_input >> ((serial_bits - 1) & 31)) & 1)) {
    serial_bits = 0;
    serial_input = 0;
    return;
  }
  if (serial_bits < header)
    return;
  serial_mode = static_cast<unsigned>((serial_input >> ((serial_bits - 3) & 31)) & 3);
  serial_address =
      static_cast<unsigned>((serial_input >> ((serial_bits - header) & 31)) & address_mask);
  const auto flush = [&] {
    serial_bits = 0;
    serial_input = 0;
  };
  const auto store_word = [&](unsigned address, unsigned word) {
    const auto offset = (address * 2) & (eeprom.size() - 1);
    eeprom[offset] = static_cast<std::uint8_t>(word >> 8);
    eeprom[offset + 1] = static_cast<std::uint8_t>(word);
  };
  if (serial_mode == 2) {
    read_word();
    return;
  }
  if (serial_mode == 3) {
    if (serial_writable) {
      store_word(serial_address, 65535);
      serial_busy = 4;
    }
    flush();
    return;
  }
  const unsigned operation = static_cast<unsigned>((serial_input >> ((serial_bits - 5) & 31)) & 3);
  if (!serial_mode && (operation == 0 || operation == 3)) {
    serial_writable = operation == 3;
    flush();
    return;
  }
  if (!serial_mode && operation == 2) {
    if (serial_writable) {
      std::fill(eeprom.begin(), eeprom.end(), 255);
      serial_busy = 8;
    }
    flush();
    return;
  }
  if (serial_bits < header + 16)
    return;
  if (serial_writable) {
    const auto word =
        static_cast<unsigned>((serial_input >> ((serial_bits - header - 16) & 31)) & 65535);
    if (serial_mode == 1) {
      store_word(serial_address, word);
      serial_busy = 4;
    } else {
      for (unsigned address = 0; address < eeprom.size() / 2; ++address)
        store_word(address, word);
      serial_busy = 16;
    }
  }
  flush();
}

} // namespace cupid::n64
