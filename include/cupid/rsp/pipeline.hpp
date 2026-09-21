#pragma once

#include "cupid/types.hpp"

#include <array>

namespace cupid {

class RspPipeline {
  public:
    void reset();
    void redirect();
    [[nodiscard]] bool advance_branch_wait();
    void fetch(u32 first, u32 second, bool single_step);
    [[nodiscard]] bool advance_operand_wait();
    [[nodiscard]] unsigned size() const {
        return count_;
    }
    [[nodiscard]] u32 instruction(unsigned index) const {
        return words_[index];
    }
    void retire(bool taken_delay_slot, u32 next_pc);

  private:
    enum : unsigned {
        vector_alu = 1U,
        load = 2U,
        store = 4U,
        branch = 8U,
        vector_nop = 16U,
        nop_conflict = 32U,
    };

    struct Ports {
        u32 scalar_reads{};
        u32 scalar_result{};
        u32 vector_reads{};
        u32 vector_result{};
        u32 control_reads{};
        u32 control_result{};
        u32 field_reads{};
        unsigned flags{};
    };

    struct Stage {
        u32 scalar_result{};
        u32 vector_result{};
        bool load{};
    };

    [[nodiscard]] static Ports decode(u32 word);
    [[nodiscard]] static bool can_pair(const Ports& first, const Ports& second);
    void advance(Stage stage);

    std::array<Stage, 3> previous_{};
    std::array<u32, 2> words_{};
    Ports current_{};
    unsigned count_{};
    bool single_issue_{};
    bool branch_wait_{};
};

} // namespace cupid
