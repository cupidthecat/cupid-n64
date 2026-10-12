#include "core/debug/diagnostics.hpp"
#include "core/system/console.hpp"
#include <bit>
#include <iomanip>
#include <sstream>

namespace cupid::n64 {

void Diagnostics::register_log(std::uint32_t mask, unsigned code) {
  const auto &rsp = console_->signal();
  const auto &state = rsp.state_;
  std::ostringstream output;
  output.imbue(std::locale::classic());
  const auto value = [&](std::uint32_t number, unsigned bits) {
    if (code & 4)
      output << std::dec
             << (bits == 16 ? std::int64_t(static_cast<std::int16_t>(number))
                            : std::int64_t(std::bit_cast<std::int32_t>(number)));
    else
      output << std::hex << std::setfill('0') << std::setw(bits / 4) << number;
  };
  if (!mask)
    mask = ~0u;
  if ((code & 3) == 0) {
    const auto &dp = console_->display();
    const auto &command = dp.command();
    const auto &status = rsp.status_;
    const std::array<std::uint32_t, 16> registers{
        rsp.current_.local_address,
        rsp.current_.dram_address,
        rsp.current_.length | (rsp.current_.count << 12) | (rsp.current_.skip << 20),
        rsp.current_.length | (rsp.current_.count << 12) | (rsp.current_.skip << 20),
        unsigned(status.halted) | (unsigned(status.broken) << 1) | (unsigned(rsp.dma_busy()) << 2) |
            (unsigned(rsp.dma_full()) << 3) | (unsigned(status.io_full) << 4) |
            (unsigned(status.single_step) << 5) | (unsigned(status.interrupt_on_break) << 6) |
            (unsigned(status.signals) << 7),
        unsigned(rsp.dma_full()),
        unsigned(rsp.dma_busy()),
        unsigned(status.semaphore),
        command.start,
        command.end,
        command.current,
        dp.status(),
        dp.clock_counter(rsp.clock_),
        command.buffer_busy,
        command.pipe_busy,
        command.tmem_busy};
    for (unsigned n = 0; n < registers.size(); ++n)
      if (mask & (1u << n)) {
        output << "COP0 " << std::dec << n << " = ";
        value(registers[n], 32);
        output << '\n';
      }
  } else if ((code & 3) == 2 || (code & 3) == 3) {
    for (unsigned n = 0; n < 32; ++n)
      if (mask & (1u << n)) {
        output << ((code & 3) == 2 ? "V" : "R") << std::dec << n << " = ";
        if ((code & 3) == 2) {
          for (unsigned lane = 0; lane < 8; ++lane) {
            if (lane)
              output << ' ';
            value(state.vectors[n].lanes[lane], 16);
          }
        } else
          value(state.gpr[n], 32);
        output << '\n';
      }
  }
  if (code & 16) {
    output << "PC = ";
    value(rsp.pc_, 32);
    output << "\nVCO = ";
    value(state.carry_low | (unsigned(state.carry_high) << 8), 16);
    output << "\nVCC = ";
    value(state.compare_low | (unsigned(state.compare_high) << 8), 16);
    output << "\nVCE = ";
    value(state.extension, 16);
    output << "\nACC =";
    for (unsigned lane = 0; lane < 8; ++lane)
      output << ' ' << std::hex << std::setfill('0') << std::setw(12)
             << state.accumulator.get(lane);
    output << '\n';
  }
  text(DebugProcessor::Rsp, output.str());
}

} // namespace cupid::n64
