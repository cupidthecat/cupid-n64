#pragma once

#include <array>
#include <cstdint>

namespace test::memory_bus {

constexpr std::uint32_t device_id(unsigned id) {
  constexpr std::array<unsigned, 16> positions{26, 27, 28, 29, 30, 31, 23, 8,
                                               9,  10, 11, 12, 13, 14, 15, 7};
  std::uint32_t value = 0;
  for (unsigned bit = 0; bit < positions.size(); ++bit)
    value |= ((id >> bit) & 1u) << positions[bit];
  return value;
}

constexpr std::uint32_t current_mode(unsigned current, bool automatic = false) {
  constexpr std::array<unsigned, 6> positions{6, 14, 22, 7, 15, 23};
  std::uint32_t value = automatic ? 0x82000000u : 0x02000000u;
  for (unsigned bit = 0; bit < positions.size(); ++bit)
    value |= (((current ^ 63) >> bit) & 1u) << positions[bit];
  return value;
}

template <class Probe> void initialize(Probe &p) {
  p.ri_write(8, 0);
  p.ri_write(12, 0x14);
  p.chip_write(0x03f80008, 0x00080008, 16);
  const auto count = p.size() / 0x200000;
  for (unsigned chip = 0; chip < count; ++chip) {
    p.chip_write(0x03f0000c, current_mode(63));
    p.chip_write(0x03f00004, device_id((count + chip) * 2));
  }
  for (unsigned chip = 0; chip < count; ++chip)
    p.chip_write(0x03f00004 + (count + chip) * 0x800, device_id(chip * 2));
}

template <class Probe> void state(Probe &p) {
  p.emit(p.identity());
  p.emit(p.active());
  for (unsigned index = 0; index < 8; ++index)
    p.emit(p.ri_read(index * 4));
  p.emit(p.mi_read());
}

template <class Probe> void registers(Probe &p) {
  for (unsigned select : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 16u, 17u, 511u})
    for (unsigned offset :
         {0u, 4u, 8u, 12u, 16u, 20u, 24u, 28u, 32u, 36u, 40u, 60u, 0x200u, 0x3fcu}) {
      p.emit(p.load(0x03f00000 + select * 0x400 + offset, 4));
      p.emit(p.read_clocks());
    }
}

template <class Probe> void scenarios(Probe &p) {
  for (auto seed : {0ull, 0x123456789abcdef0ull, 0xffffffffffffffffull})
    for (bool expansion : {false, true}) {
      p.begin(seed, expansion);
      state(p);
      for (unsigned index = 0; index < 8; ++index) {
        p.ri_write(index * 4 + 0x40, 0xabcdef01u + index);
        state(p);
      }
      p.power(true);
      state(p);
      p.power(false);
      state(p);
      p.finish();

      p.begin(seed, expansion);
      p.ri_write(8, 0);
      p.ri_write(12, 0x14);
      for (unsigned length : {0u, 1u, 7u, 8u, 15u}) {
        p.chip_write(0x03f80008, 0x00080008, length);
        p.chip_write(0x03f0000c, current_mode(63), length);
        registers(p);
      }
      p.chip_write(0x03f80008, 0x00080008, 16);
      p.chip_write(0x03f0000c, current_mode(63));
      registers(p);
      p.chip_write(0x03f80200, 0x89abcdef);
      registers(p);
      p.finish();

      for (unsigned id :
           {0u, 1u, 2u, 3u, 4u, 6u, 16u, 31u, 32u, 63u, 64u, 127u, 128u, 255u, 256u, 511u}) {
        p.begin(seed, expansion);
        initialize(p);
        for (unsigned chip = 0; chip < p.size() / 0x200000; ++chip)
          p.store(chip * 0x200000 + 0x1000, 8, 0x1122334455667788ull + chip);
        p.chip_write(0x03f00804, device_id(id));
        registers(p);
        for (unsigned address :
             {0x1000u, 0x201000u, 0x401000u, 0x601000u, (id >> 1) * 0x200000 + 0x1000}) {
          p.emit(p.raw(address, 8));
          p.emit(p.ri_read(24));
        }
        p.ri_write(12, 0);
        p.emit(p.raw((id >> 1) * 0x200000 + 0x1000, 8));
        state(p);
        p.ri_write(12, 0x14);
        p.power(true);
        registers(p);
        p.emit(p.raw((id >> 1) * 0x200000 + 0x1000, 8));
        state(p);
        p.power(false);
        registers(p);
        p.emit(p.raw(0x1000, 8));
        state(p);
        p.finish();
      }

      p.begin(seed, expansion);
      initialize(p);
      for (unsigned chip = 0; chip < p.size() / 0x200000; ++chip)
        for (bool automatic : {false, true}) {
          const auto address = chip * 0x200000 + 0x1000;
          for (unsigned current = 0; current < 64; ++current) {
            p.chip_write(0x03f0000c + chip * 0x800, current_mode(current, automatic));
            for (auto pattern :
                 {0ull, 0xffffffffffffffffull, 0x0123456789abcdefull, 0x8001000180010001ull}) {
              p.store(address, 8, pattern);
              for (unsigned bytes : {1u, 2u, 4u, 8u})
                p.emit(p.raw(address, bytes));
              p.mi_write(0x400);
              p.emit(p.load(address, 8));
              p.mi_write(0x200);
              p.emit(p.random());
            }
            p.emit(p.chip_read(0x03f0000c + chip * 0x800));
            p.emit(p.identity());
          }
          p.chip_write(0x03f0000c + chip * 0x800, current_mode(63));
          p.finish();
        }

      p.begin(seed, expansion);
      initialize(p);
      for (unsigned boundary = 0; boundary < 3; ++boundary)
        for (bool ebus : {false, true})
          for (unsigned bytes : {1u, 2u, 4u, 8u})
            for (unsigned offset = 0; offset < 8; offset += bytes) {
              const auto base = boundary == 0 ? 0x1200u : boundary == 1 ? 0x17f8u : p.size() - 8;
              const auto bank = base & ~0x7ffu;
              const auto head = boundary == 0 ? 0x1200u : boundary == 1 ? 0x1000u : p.size() - 128;
              const auto tail = boundary == 0 ? 0x1280u : boundary == 1 ? 0x1780u : 0u;
              for (unsigned length = 1; length <= 128; ++length) {
                for (unsigned word = 0; word < 512; ++word)
                  p.store(bank + word * 4, 4, 0x12345678u ^ (word * 0x1020409u));
                for (unsigned word = 0; word < 32; ++word)
                  p.store(tail + word * 4, 4, 0x87654321u ^ (word * 0x8040201u));
                p.mi_write(0x100 | (ebus ? 0x400 : 0x200) | (length - 1));
                p.bus_store(base + offset, bytes, 0x0123456789abcdefull);
                p.emit(p.mi_read());
                for (auto address : {head, tail}) {
                  for (unsigned word = 0; word < 32; ++word)
                    p.emit(p.raw(address + word * 4, 4));
                  for (unsigned half = 0; half < 64; ++half)
                    p.emit(p.hidden((address >> 1) + half));
                }
                p.bus_store(base + offset, bytes, 0xfedcba9876543210ull);
                p.emit(p.mi_read());
                p.emit(p.raw(base, 8));
                p.emit(p.hidden(base >> 1));
                p.emit(p.hidden((base >> 1) + 1));
                p.emit(p.hidden((base >> 1) + 2));
                p.emit(p.hidden((base >> 1) + 3));
              }
              p.finish();
            }
    }
}

} // namespace test::memory_bus
