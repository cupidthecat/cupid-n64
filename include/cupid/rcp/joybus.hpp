#pragma once

#include "cupid/types.hpp"

#include <array>

namespace cupid {

class Bus;

class Joybus {
  public:
    explicit Joybus(Bus& bus) : bus_(bus) {}
    void reset();
    void configure();
    void execute();

  private:
    Bus& bus_;
    struct Channel {
        u8 offset{};
        bool skip{true};
        bool reset{};
    };
    std::array<Channel, 5> channels_{};
};

} // namespace cupid
