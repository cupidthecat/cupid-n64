#pragma once

#include "cupid/types.hpp"

#include <array>
#include <span>

namespace cupid {

class CartridgeRtc {
  public:
    using Registers = std::array<u8, 32>;
    static constexpr u64 cycles_per_second = 62'500'000;

    explicit CartridgeRtc(Registers registers);
    [[nodiscard]] const Registers& registers() const {
        return registers_;
    }
    [[nodiscard]] bool running() const {
        return (registers_[1] & 4U) == 0;
    }
    [[nodiscard]] u64 next_tick() const {
        return cycles_per_second - fraction_;
    }
    void reset_clock();
    void tick(u64 cycles);
    bool execute(std::span<const u8> input, std::span<u8> output);

  private:
    Registers registers_;
    u64 fraction_{};
    void advance_second();
    [[nodiscard]] u8 status() const {
        return running() ? u8{0} : u8{0x80};
    }
};

} // namespace cupid
