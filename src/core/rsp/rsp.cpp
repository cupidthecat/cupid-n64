#include "core/rsp/rsp.hpp"
#include <utility>

namespace cupid::n64 {

Rsp::Rsp(Rdram &ram, MipsInterface &interrupts, RandomGenerator &random)
    : ram_(ram), interrupts_(interrupts), random_(random) {
  power();
}

void Rsp::power() {
  memory_.fill(0);
  status_ = {};
  state_ = {};
  pipeline_ = {};
  pending_ = current_ = {};
  pc_ = 0;
  pipeline_pc_ = next_pc_ = 4;
  clock_ = 0;
  delay_slot_ = next_delay_slot_ = false;
  dma_clock_ = 0;
  busy_read_ = busy_write_ = full_read_ = full_write_ = false;
}

void Rsp::connect_sync(std::function<void()> callback) {
  sync_ = std::move(callback);
}

void Rsp::connect_invalidation(std::function<void(std::uint32_t, unsigned)> callback) {
  invalidate_ = std::move(callback);
}

void Rsp::connect_display(std::function<std::uint32_t(unsigned)> read,
                          std::function<void(unsigned, std::uint32_t)> write) {
  display_read_ = std::move(read);
  display_write_ = std::move(write);
}

std::uint64_t Rsp::read_local(std::uint32_t address, unsigned bytes) const {
  const auto region = address & 0x1000;
  address = (address & 0xfff) & ~(bytes - 1u);
  std::uint64_t value = 0;
  for (unsigned n = 0; n < bytes; ++n)
    value = (value << 8) | memory_[region | (address + n)];
  return value;
}

void Rsp::write_local(std::uint32_t address, unsigned bytes, std::uint64_t value) {
  const auto region = address & 0x1000;
  address = (address & 0xfff) & ~(bytes - 1u);
  if (region && invalidate_)
    invalidate_(address, bytes);
  for (unsigned n = 0; n < bytes; ++n)
    memory_[region | (address + n)] = static_cast<std::uint8_t>(value >> ((bytes - n - 1) * 8));
}

BusRead Rsp::read(std::uint32_t address, unsigned bytes) {
  std::uint64_t value = read_word(address);
  if (bytes == 8)
    value = (value << 32) | read_word(address + 4);
  else if (bytes != 4)
    value >>= (4 - bytes - (address & (4 - bytes))) * 8;
  return {value, 40};
}

BusWrite Rsp::write(std::uint32_t address, unsigned bytes, std::uint64_t value,
                    std::int64_t clock_difference) {
  auto word = static_cast<std::uint32_t>(bytes == 8 ? value >> 32 : value);
  if (bytes != 4 && bytes != 8)
    word <<= (4 - bytes - (address & (4 - bytes))) * 8;
  write_word(address, word, clock_difference);
  return {};
}

} // namespace cupid::n64
