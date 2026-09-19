#pragma once

#include "cupid/types.hpp"

#include <array>
#include <optional>
#include <vector>

namespace cupid {

class Rdram {
  public:
    explicit Rdram(std::vector<u8>& bytes);
    void reset(bool warm = false);
    void set_bus_active(bool active);
    [[nodiscard]] bool bus_active() const {
        return active_;
    }
    [[nodiscard]] bool acknowledgement_error() const {
        return (errors_ & 1U) != 0;
    }
    [[nodiscard]] u32 errors() const {
        return errors_;
    }
    void clear_error() {
        errors_ = 0;
    }
    [[nodiscard]] u32 bank_status() const;
    void invalidate_banks();
    [[nodiscard]] bool refresh_banks();

    [[nodiscard]] u32 read_register(u32 address) const;
    void write_register(u32 address, u32 value, unsigned repeat_length = 0);
    [[nodiscard]] u64 read(u32 address, unsigned width, bool ebus = false) const;
    void write(u32 address, unsigned width, u64 value, bool ebus = false);
    [[nodiscard]] u8 hidden_pair(u32 address) const;
    void set_hidden_pair(u32 address, u8 value);

  private:
    struct Bank {
        u16 row{};
        bool valid{};
        bool dirty{};
    };

    struct Chip {
        std::array<u32, 10> registers{};
        u32 row{};
        u16 device_id{};
        u8 write_delay{4};
        u8 current{};
        u8 latched_current{};
        u8 current_low{8};
        u8 current_high{14};
        bool present{};
        bool enabled{};
        bool auto_current{};
    };

    std::vector<u8>& bytes_;
    std::vector<u8> hidden_;
    std::array<Chip, 4> chips_{};
    mutable std::array<Bank, 8> banks_{};
    mutable u64 noise_{0x2360ed051fc65da4ULL};
    mutable u32 errors_{};
    bool active_{};
    bool identity_mapping_{};

    void refresh_mapping();
    void track_access(u32 address, bool write) const;
    [[nodiscard]] std::optional<unsigned> select_chip(u32 address) const;
    [[nodiscard]] std::optional<u32> translate(u32 address) const;
    [[nodiscard]] u64 read_reliability(u64 value, unsigned chip) const;
    [[nodiscard]] static u8 decode_current(u32 mode);
    [[nodiscard]] static u32 encode_current(u8 current);
    [[nodiscard]] static u16 decode_device_id(u32 value);
    void write_chip(Chip& chip, unsigned index, u32 value, unsigned repeat_length);
    void write_hidden_bit(u32 address, unsigned bit);
    [[nodiscard]] u32 read_hidden_word(u32 address) const;
};

} // namespace cupid
