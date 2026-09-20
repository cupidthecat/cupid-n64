#pragma once

#include "cupid/cartridge/game_boy.hpp"

namespace cupid {

class TransferPak {
  public:
    void insert(GameBoyCartridge cartridge);
    void remove();
    void disconnect();
    [[nodiscard]] GameBoyCartridge* cartridge() {
        return cartridge_ ? &*cartridge_ : nullptr;
    }
    [[nodiscard]] const GameBoyCartridge* cartridge() const {
        return cartridge_ ? &*cartridge_ : nullptr;
    }
    [[nodiscard]] u8 read(u16 address);
    void write(u16 address, u8 value);

  private:
    std::optional<GameBoyCartridge> cartridge_;
    bool enabled_{}, access_{};
    u8 bank_{}, reset_{};
};

} // namespace cupid
