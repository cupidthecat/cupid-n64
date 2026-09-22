#pragma once

#include "cupid/types.hpp"

#include <array>
#include <span>

namespace cupid {

enum class CicModel {
    Nus6101,
    Nus6102,
    Nus7102,
    Nus6103,
    Nus6105,
    Nus6106,
    Nus5101,
    Nus5167,
    Nus8303,
    Nus8401,
    NusDDUS,
};

class Cic {
  public:
    bool detect(std::span<const u8> normalized_rom);
    void configure(CicModel model, bool pal = false);
    void reset();

    [[nodiscard]] CicModel model() const {
        return model_;
    }
    [[nodiscard]] u8 seed() const {
        return seed_;
    }
    [[nodiscard]] u64 checksum() const {
        return checksum_;
    }
    [[nodiscard]] bool pal() const {
        return pal_;
    }
    [[nodiscard]] bool recognized() const {
        return recognized_;
    }
    [[nodiscard]] bool disk_drive() const;
    [[nodiscard]] bool verify_checksum(std::span<const u8, 6> bytes) const;
    void challenge(std::span<u8, 15> bytes) const;

    [[nodiscard]] std::array<u8, 6> seed_packet() const;
    [[nodiscard]] std::array<u8, 16> checksum_packet(u16 entropy) const;
    [[nodiscard]] static u64 boot_checksum(std::span<const u8> bootcode, u8 seed);

  private:
    CicModel model_{CicModel::Nus6102};
    u8 seed_{0x3f};
    u64 checksum_{0xa536c0f1d859ULL};
    bool pal_{};
    bool recognized_{};
};

} // namespace cupid
