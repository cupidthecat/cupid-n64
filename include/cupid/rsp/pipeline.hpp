#pragma once

#include "cupid/types.hpp"

#include <array>
#include <bitset>
#include <span>

namespace cupid {

class Rsp;

class RspPipeline {
  public:
    enum class Operation : u8 {
        None,
        Sll,
        Srl,
        Sra,
        Sllv,
        Srlv,
        Srav,
        Jr,
        Jalr,
        Break,
        Addu,
        Subu,
        And,
        Or,
        Xor,
        Nor,
        Slt,
        Sltu,
        Bltz,
        Bgez,
        Bltzal,
        Bgezal,
        J,
        Jal,
        Beq,
        Bne,
        Blez,
        Bgtz,
        Addiu,
        Slti,
        Sltiu,
        Andi,
        Ori,
        Xori,
        Lui,
        Cop0,
        Cop2,
        Lb,
        Lh,
        Lw,
        Lbu,
        Lhu,
        Sb,
        Sh,
        Sw,
        VectorLoad,
        VectorStore,
        ReservedSpecial,
    };

    enum class LocalIssue : u8 {
        Blocked,
        Advanced,
        Ready,
    };

    void reset();
    void redirect();
    [[nodiscard]] bool advance_branch_wait();
    [[nodiscard]] LocalIssue local_issue(u32 first, u32 second, u32 address = 0);
    [[nodiscard]] LocalIssue local_issue();
    void fetch(u32 first, u32 second, bool single_step, u32 address = 0);
    [[nodiscard]] bool advance_operand_wait();
    // Consume only operand bubbles, leaving the fetched packet pending at a
    // scheduling boundary. The returned count never includes its issue cycle.
    [[nodiscard]] unsigned advance_operand_wait(unsigned maximum_cycles);
    [[nodiscard]] unsigned size() const {
        return count_;
    }
    [[nodiscard]] u32 instruction(unsigned index) const {
        return decoded_[current_index_].words[index];
    }
    [[nodiscard]] Operation operation(unsigned index) const {
        return decoded_[current_index_].operations[index];
    }
    void retire(bool taken_delay_slot, u32 next_pc);

  private:
    friend class Rsp;

    struct LocalWindow {
        std::bitset<1024> validated{};
    };

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
        bool operator==(const Stage&) const = default;
    };

    struct DecodedFetch {
        std::array<u32, 2> words{};
        struct Dependencies {
            u32 scalar_reads{};
            u32 scalar_result{};
            u32 vector_reads{};
            u32 vector_result{};
        } ports;
        std::array<Operation, 2> operations{};
        u8 count{};
        bool issued_local{};
        u8 flags{};
    };
    static_assert(sizeof(DecodedFetch) == 32);

    // Preserve the latched fetch even if a later fetch replaces its cache slot.
    struct Snapshot {
        DecodedFetch fetched{};
        u64 revision{};
        std::array<Stage, 3> previous{};
        unsigned current_index{};
        unsigned count{};
        bool single_issue{};
        bool branch_wait{};
    };

    [[nodiscard]] Snapshot snapshot() const {
        return {current_fetch(), decoded_revision_[current_index_],
                previous_,       current_index_,
                count_,          single_issue_,
                branch_wait_};
    }
    void restore(const Snapshot& snapshot) {
        decoded_[snapshot.current_index] = snapshot.fetched;
        decoded_revision_[snapshot.current_index] = snapshot.revision;
        previous_ = snapshot.previous;
        current_index_ = snapshot.current_index;
        count_ = snapshot.count;
        single_issue_ = snapshot.single_issue;
        branch_wait_ = snapshot.branch_wait;
    }

    struct DecodedWord {
        Ports ports{};
        Operation operation{};
    };

    [[nodiscard]] static DecodedWord decode(u32 word);
    [[nodiscard]] static bool can_pair(const Ports& first, const Ports& second);
    [[nodiscard]] static bool instruction_is_local(u32 word);
    [[nodiscard]] static unsigned cache_index(u32 address, bool pairing_allowed);
    [[nodiscard]] DecodedFetch& current_fetch();
    [[nodiscard]] const DecodedFetch& current_fetch() const;
    [[nodiscard]] DecodedFetch& prepare(u32 first, u32 second, bool pairing_allowed, u32 address);
    [[nodiscard]] DecodedFetch& prepare(std::span<const u8, 4096> imem, u64 revision, bool pairing_allowed,
                                        u32 address);
    [[nodiscard]] LocalIssue local_issue(std::span<const u8, 4096> imem, LocalWindow& window, u32 address);
    [[nodiscard]] LocalIssue local_issue(std::span<const u8, 4096> imem, u64 revision, u32 address);
    void fetch(std::span<const u8, 4096> imem, u64 revision, bool single_step, u32 address);
    void advance(Stage stage);

    std::array<Stage, 3> previous_{};
    unsigned current_index_{};
    unsigned count_{};
    bool single_issue_{};
    bool branch_wait_{};
    std::array<DecodedFetch, 2048> decoded_{};
    std::array<u64, 2048> decoded_revision_{};
};

} // namespace cupid
