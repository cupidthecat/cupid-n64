#pragma once

#include "cupid/types.hpp"

#include <array>
#include <optional>
#include <span>
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

    // Row tracking timestamps count RCP cycles so requesters can tell whether
    // another device touched a bank since their previous access.
    void advance_clock(u64 rcp_cycles) {
        clock_ += rcp_cycles;
    }
    [[nodiscard]] u64 clock() const {
        return clock_;
    }
    [[nodiscard]] bool row_open(u32 address) const;
    [[nodiscard]] u64 bank_access_clock(u32 address) const;
    void open_row(u32 address) const {
        track_access(address, false);
    }

    [[nodiscard]] u32 read_register(u32 address) const;
    void write_register(u32 address, u32 value, unsigned repeat_length = 0);
    [[nodiscard]] u64 read(u32 address, unsigned width, bool ebus = false) const;
    void write(u32 address, unsigned width, u64 value, bool ebus = false);
    void read_burst(u32 address, std::span<u8> bytes) const;
    void write_burst(u32 address, std::span<const u8> bytes);
    [[nodiscard]] u8 hidden_pair(u32 address) const;
    [[nodiscard]] std::span<const u8> hidden_memory() const {
        return hidden_;
    }
    void set_hidden_pair(u32 address, u8 value);

    struct BankAccessSummary {
        struct Entry {
            u64 last_access{};
            u16 first_row{};
            u16 last_row{};
            bool visited{};
            bool changed_row{};
            bool dirty{};
        };
        std::array<Entry, 8> banks{};
    };

    // A synchronous raster task owns disjoint memory cells. It records row
    // effects locally so the caller can merge them in the original draw order.
    class BankAccessScope {
      public:
        BankAccessScope(const Rdram& memory, BankAccessSummary& summary);
        ~BankAccessScope();
        BankAccessScope(const BankAccessScope&) = delete;
        BankAccessScope& operator=(const BankAccessScope&) = delete;

      private:
        friend class Rdram;
        const Rdram& memory_;
        BankAccessSummary& summary_;
        BankAccessScope* previous_;
        static thread_local BankAccessScope* current_;
    };

    [[nodiscard]] bool direct_access_ready() const {
        return identity_mapping_;
    }
    void merge_bank_accesses(const BankAccessSummary& summary) const;

  private:
    struct Bank {
        u64 last_access{};
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
    u64 clock_{};
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
