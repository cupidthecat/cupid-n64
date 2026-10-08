#include "core/cpu/cpu.hpp"

namespace cupid::n64 {

Cpu::Segment Cpu::segment(std::uint64_t address) const {
  const auto current_mode = mode();
  if (!extended_addressing()) {
    const auto low = static_cast<std::uint32_t>(address);
    if (low <= 0x7fffffff)
      return Segment::Mapped;
    if (current_mode == Mode::User)
      return Segment::Invalid;
    if (current_mode == Mode::Supervisor) {
      return low >= 0xc0000000 && low <= 0xdfffffff ? Segment::Mapped : Segment::Invalid;
    }
    if (low <= 0x9fffffff)
      return Segment::Cached;
    if (low <= 0xbfffffff)
      return Segment::Direct;
    return Segment::Mapped;
  }
  if (address <= 0x000000ffffffffffull)
    return Segment::Mapped;
  if (current_mode == Mode::User)
    return Segment::Invalid;
  if (address >= 0x4000000000000000ull && address <= 0x400000ffffffffffull)
    return Segment::Mapped;
  if (current_mode == Mode::Supervisor) {
    return address >= 0xffffffffc0000000ull && address <= 0xffffffffdfffffffull ? Segment::Mapped
                                                                                : Segment::Invalid;
  }
  if (address >= 0x8000000000000000ull && address <= 0xbfffffffffffffffull) {
    if (address & 0x07ffffff00000000ull)
      return Segment::Invalid;
    return ((address >> 59) & 7) == 2 ? Segment::Direct32 : Segment::Cached32;
  }
  if (address >= 0xc000000000000000ull && address <= 0xc00000ff7fffffffull)
    return Segment::Mapped;
  if (address < 0xffffffff80000000ull)
    return Segment::Invalid;
  if (address <= 0xffffffff9fffffffull)
    return Segment::Cached;
  if (address <= 0xffffffffbfffffffull)
    return Segment::Direct;
  return Segment::Mapped;
}

std::optional<Address> Cpu::translate(std::uint64_t address, unsigned bytes, bool store,
                                      bool alignment) {
  if (alignment &&
      ((address & (bytes - 1)) ||
       (!extended_addressing() && sign_word(static_cast<std::uint32_t>(address)) != address))) {
    advance_clocks(2);
    address_exception(address);
    raise(store ? Exception::AddressStore : Exception::AddressLoad);
    return {};
  }
  if (address >= 0xffffffff80000000ull && address < 0xffffffff83f00000ull)
    return Address{static_cast<std::uint32_t>(address & 0x3effffff), true};
  switch (segment(address)) {
  case Segment::Invalid:
    address_exception(address);
    raise(store ? Exception::AddressStore : Exception::AddressLoad);
    return {};
  case Segment::Cached:
    return Address{static_cast<std::uint32_t>(address & 0x1fffffff), true};
  case Segment::Direct:
    return Address{static_cast<std::uint32_t>(address & 0x1fffffff), false};
  case Segment::Cached32:
    return Address{static_cast<std::uint32_t>(address), true};
  case Segment::Direct32:
    return Address{static_cast<std::uint32_t>(address), false};
  case Segment::Mapped:
    break;
  }
  return translate_tlb(address, store);
}

std::optional<std::uint64_t> Cpu::read(std::uint64_t address, unsigned bytes, bool instruction) {
  const auto access = translate(address, bytes, false);
  if (!access)
    return {};
  if (instruction)
    advance_clocks(2);
  const auto physical = access->physical ^ (little_endian() ? 8 - bytes : 0);
  if (access->cached)
    return cache_read(address, physical, bytes, instruction);
  const auto transfer = bus_.read(physical, bytes);
  advance_clocks(transfer.clocks);
  if (!transfer.success) {
    if (!bus_.frozen())
      raise(instruction ? Exception::BusInstruction : Exception::BusData);
    return {};
  }
  return transfer.value;
}

bool Cpu::write(std::uint64_t address, unsigned bytes, std::uint64_t value, bool alignment) {
  const auto access = translate(address, bytes, true, alignment);
  if (!access)
    return false;
  const auto physical = access->physical ^ (little_endian() ? 8 - bytes : 0);
  if (access->cached)
    return cache_write(address, physical, bytes, value);
  const auto transfer = bus_.write(physical, bytes, value);
  advance_clocks(transfer.clocks);
  if (!transfer.success && !bus_.frozen())
    raise(Exception::BusData);
  return transfer.success;
}

void Cpu::load_merge(unsigned reg, std::uint64_t address, unsigned bytes, bool left) {
  if (bytes == 8 && !require_doubleword())
    return;
  const auto memory = read(address & ~std::uint64_t(bytes - 1), bytes);
  if (!memory)
    return;
  const auto offset = static_cast<unsigned>(address & (bytes - 1));
  const auto lane = little_endian() ? bytes - 1 - offset : offset;
  const auto shift = (left ? lane : bytes - 1 - lane) * 8;
  const auto width_mask = bytes == 8 ? ~0ull : 0xffffffffull;
  const auto write_mask = left ? width_mask << shift : width_mask >> shift;
  const auto value = left ? *memory << shift : *memory >> shift;
  const auto result = (state_.gpr[reg] & ~write_mask) | (value & write_mask);
  if (bytes == 4 && (left || shift == 0))
    state_.gpr[reg] = sign_word(static_cast<std::uint32_t>(result));
  else
    state_.gpr[reg] = result;
}

void Cpu::load_store(std::uint32_t instruction) {
  const auto operation = instruction >> 26;
  const auto rs = (instruction >> 21) & 31;
  const auto rt = (instruction >> 16) & 31;
  const auto immediate = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(instruction));
  const auto address = state_.gpr[rs] + static_cast<std::uint64_t>(std::int64_t(immediate));
  const auto value = state_.gpr[rt];
  switch (operation) {
  case 0x1a:
    return load_merge(rt, address, 8, true);
  case 0x1b:
    return load_merge(rt, address, 8, false);
  case 0x22:
    return load_merge(rt, address, 4, true);
  case 0x26:
    return load_merge(rt, address, 4, false);
  case 0x2a:
    return store_merge(value, address, 4, true);
  case 0x2c:
    return store_merge(value, address, 8, true);
  case 0x2d:
    return store_merge(value, address, 8, false);
  case 0x2e:
    return store_merge(value, address, 4, false);
  case 0x2f:
    return cache_operation(rt, address);
  case 0x30:
  case 0x34:
    return load_linked(rt, address, operation == 0x34);
  case 0x28:
    write(address, 1, value);
    return;
  case 0x29:
    write(address, 2, value);
    return;
  case 0x2b:
    write(address, 4, value);
    return;
  case 0x3f:
    if (require_doubleword())
      write(address, 8, value);
    return;
  case 0x38:
  case 0x3c: {
    if (operation == 0x3c && !require_doubleword())
      return;
    const bool linked = llbit_;
    const bool success = linked && write(address, operation == 0x38 ? 4 : 8, value);
    state_.gpr[rt] = success;
    return;
  }
  case 0x31:
  case 0x35:
  case 0x39:
  case 0x3d:
    return fpu_memory(operation, rt, address);
  case 0x32:
  case 0x36:
  case 0x3a:
  case 0x3e:
    return cop2_invalid();
  case 0x33:
  case 0x3b:
    return raise(Exception::ReservedInstruction);
  default:
    break;
  }
  const bool wide = operation == 0x37;
  if (wide && !require_doubleword())
    return;
  const unsigned bytes = operation == 0x20 || operation == 0x24   ? 1
                         : operation == 0x21 || operation == 0x25 ? 2
                         : wide                                   ? 8
                                                                  : 4;
  const auto data = read(address, bytes);
  if (!data)
    return;
  switch (operation) {
  case 0x20:
    state_.gpr[rt] = static_cast<std::uint64_t>(
        std::int64_t(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(*data))));
    break;
  case 0x21:
    state_.gpr[rt] = static_cast<std::uint64_t>(
        std::int64_t(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(*data))));
    break;
  case 0x23:
    state_.gpr[rt] = sign_word(static_cast<std::uint32_t>(*data));
    break;
  default:
    state_.gpr[rt] = *data;
    break;
  }
}

} // namespace cupid::n64
