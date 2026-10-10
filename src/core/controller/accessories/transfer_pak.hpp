#pragma once

#include <cstdint>
#include <memory>

namespace cupid::n64 {

class TransferCartridge {
public:
  virtual ~TransferCartridge() = default;
  virtual bool present() const = 0;
  virtual void power() = 0;
  virtual std::uint8_t read(std::uint16_t address) = 0;
  virtual void write(std::uint16_t address, std::uint8_t value) = 0;
};

class TransferPak {
public:
  void connect(std::shared_ptr<TransferCartridge> cartridge = {});
  std::uint8_t read(std::uint16_t address);
  void write(std::uint16_t address, std::uint8_t value);

private:
  friend class CoreState;
  std::shared_ptr<TransferCartridge> cartridge_;
  unsigned bank_ = 0;
  unsigned reset_ = 0;
  bool enabled_ = false;
  bool cartridge_enabled_ = false;
  bool empty_board_ = false;
};

} // namespace cupid::n64
