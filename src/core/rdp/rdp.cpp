#include "core/rdp/rdp.hpp"
#include "core/timing/frequencies.hpp"
#include <utility>

namespace cupid::n64 {

Rdp::Rdp(Rdram &ram, Rsp &rsp, MipsInterface &interrupts)
    : ram_(ram), rsp_(rsp), interrupts_(interrupts) {
  power();
}

void Rdp::power() {
  command_ = {};
  test_ = {};
  clock_ = 0;
  queue_size_ = queue_offset_ = 0;
}

void Rdp::connect(std::function<void(std::span<const std::uint32_t>)> submit,
                  std::function<void()> synchronize) {
  submit_ = std::move(submit);
  synchronize_ = std::move(synchronize);
}

void Rdp::connect_sync(std::function<void()> callback) {
  sync_ = std::move(callback);
}

void Rdp::advance(std::uint32_t clocks) {
  elapse(clocks);
  run();
}

void Rdp::run() {
  while (clock_ < 0) {
    clock_ += clock_frequency;
    command_.clock = (command_.clock + clock_frequency / 3) & 0xffffff;
  }
}

void Rdp::crash() {
  command_.crashed = true;
  command_.pipe_busy = command_.buffer_busy = 1;
}

void Rdp::sync_full() {
  if (!command_.crashed) {
    interrupts_.raise(Interrupt::Display);
    command_.buffer_busy = command_.pipe_busy = 0;
  }
  command_.start_clock = false;
}

void Rdp::flush_commands() {
  if (command_.freeze || command_.crashed)
    return;
  command_.buffer_busy = command_.pipe_busy = 1;
  command_.start_clock = true;
  if (command_.end > command_.current)
    render();
  command_.buffer_busy = 0;
  command_.ready = true;
}

} // namespace cupid::n64
