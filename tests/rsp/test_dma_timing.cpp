#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>

namespace {
using namespace cupid;

constexpr u32 sp_registers = 0x04040000U;
constexpr u32 sp_mem_addr = 0x00U;
constexpr u32 sp_dram_addr = 0x04U;
constexpr u32 sp_rd_len = 0x08U;
constexpr u32 sp_wr_len = 0x0cU;
constexpr u32 sp_status = 0x10U;
constexpr u32 sp_dma_full = 0x14U;
constexpr u32 sp_dma_busy = 0x18U;

void write_register(System& system, u32 offset, u32 value) {
    system.bus.write(sp_registers + offset, 4, value);
}

u32 read_register(System& system, u32 offset) {
    return static_cast<u32>(system.bus.read(sp_registers + offset, 4));
}

void instruction(Rsp& rsp, u32 address, u32 word) {
    write_be32(&rsp.memory[0x1000U | (address & 0xffcU)], word);
}

void prepare_copy(System& system, u32 bank, bool to_sp, u32 sp, u32 dram, u32 bytes, u8 seed) {
    for (u32 byte = 0; byte < bytes; ++byte) {
        const u32 sp_index = bank | ((sp + byte) & 0x0fffU);
        const u8 value = static_cast<u8>(seed + byte);
        system.rsp.memory[sp_index] = to_sp ? 0 : value;
        system.bus.write_ram_byte(dram + byte, to_sp ? value : 0);
    }
}

void check_destination(System& system, u32 bank, bool to_sp, u32 sp, u32 dram, u32 bytes, u8 seed) {
    for (u32 byte = 0; byte < bytes; ++byte) {
        const u32 sp_index = bank | ((sp + byte) & 0x0fffU);
        const u8 actual = to_sp ? system.rsp.memory[sp_index] : system.bus.read_ram_byte(dram + byte);
        CHECK_EQ(actual, static_cast<u8>(seed + byte));
    }
}

void check_destination_clear(System& system, u32 bank, bool to_sp, u32 sp, u32 dram, u32 bytes) {
    for (u32 byte = 0; byte < bytes; ++byte) {
        const u32 sp_index = bank | ((sp + byte) & 0x0fffU);
        const u8 actual = to_sp ? system.rsp.memory[sp_index] : system.bus.read_ram_byte(dram + byte);
        CHECK_EQ(actual, 0U);
    }
}

void start_dma(System& system, u32 bank, bool to_sp, u32 sp, u32 dram, u32 length_value) {
    write_register(system, sp_mem_addr, bank | sp);
    write_register(system, sp_dram_addr, dram);
    write_register(system, to_sp ? sp_rd_len : sp_wr_len, length_value);
}

void advance_cpu(System& system, u64 cycles, bool split) {
    if (split) {
        for (u64 cycle = 0; cycle < cycles; ++cycle)
            system.advance(1);
    } else {
        system.advance(cycles);
    }
}

} // namespace

TEST(rsp_dma_timing_rows_complete_at_one_cycle_per_eight_bytes) {
    for (const bool to_sp : {false, true}) {
        for (const u32 bank : {0U, 0x1000U}) {
            for (const u32 bytes : {8U, 16U, 32U}) {
                auto system = std::make_unique<System>();
                test::initialize_memory(*system);
                prepare_copy(*system, bank, to_sp, 0, 0x200, bytes, 0x20);
                start_dma(*system, bank, to_sp, 0, 0x200, bytes - 1U);

                const u64 deadline = bytes / 8U;
                CHECK_EQ(read_register(*system, sp_dma_busy), 1U);
                if (deadline > 1) {
                    system->rsp.tick(deadline - 1U);
                    CHECK_EQ(read_register(*system, sp_dma_busy), 1U);
                    check_destination_clear(*system, bank, to_sp, 0, 0x200, bytes);
                }

                system->rsp.tick(1);
                CHECK_EQ(read_register(*system, sp_dma_busy), 0U);
                check_destination(*system, bank, to_sp, 0, 0x200, bytes, 0x20);
                CHECK_EQ(read_register(*system, sp_mem_addr), bank | bytes);
                CHECK_EQ(read_register(*system, sp_dram_addr), 0x200U + bytes);
            }
        }
    }
}

TEST(rsp_dma_timing_queued_and_repeated_rows_keep_per_row_deadlines) {
    {
        auto system = std::make_unique<System>();
        test::initialize_memory(*system);
        prepare_copy(*system, 0, true, 0x00, 0x200, 8, 0x10);
        prepare_copy(*system, 0, true, 0x20, 0x220, 8, 0x80);

        start_dma(*system, 0, true, 0x00, 0x200, 7);
        start_dma(*system, 0, true, 0x20, 0x220, 7);
        CHECK_EQ(read_register(*system, sp_dma_busy), 1U);
        CHECK_EQ(read_register(*system, sp_dma_full), 1U);

        system->rsp.tick(1);
        check_destination(*system, 0, true, 0x00, 0x200, 8, 0x10);
        check_destination_clear(*system, 0, true, 0x20, 0x220, 8);
        CHECK_EQ(read_register(*system, sp_dma_busy), 1U);
        CHECK_EQ(read_register(*system, sp_dma_full), 0U);
        CHECK_EQ(read_register(*system, sp_mem_addr), 0x20U);
        CHECK_EQ(read_register(*system, sp_dram_addr), 0x220U);

        system->rsp.tick(1);
        check_destination(*system, 0, true, 0x20, 0x220, 8, 0x80);
        CHECK_EQ(read_register(*system, sp_dma_busy), 0U);
    }

    {
        auto system = std::make_unique<System>();
        test::initialize_memory(*system);
        prepare_copy(*system, 0x1000, false, 0, 0x200, 16, 0x40);
        start_dma(*system, 0x1000, false, 0, 0x200, 0x1007U); // Two eight-byte rows.

        system->rsp.tick(1);
        check_destination(*system, 0x1000, false, 0, 0x200, 8, 0x40);
        check_destination_clear(*system, 0x1000, false, 8, 0x208, 8);
        CHECK_EQ(read_register(*system, sp_dma_busy), 1U);

        system->rsp.tick(1);
        check_destination(*system, 0x1000, false, 0, 0x200, 16, 0x40);
        CHECK_EQ(read_register(*system, sp_dma_busy), 0U);
        CHECK_EQ(read_register(*system, sp_mem_addr), 0x1010U);
        CHECK_EQ(read_register(*system, sp_dram_addr), 0x210U);
    }
}

TEST(rsp_dma_timing_cpu_bulk_and_fragmented_advances_match_rcp_ratio) {
    for (const bool split : {false, true}) {
        auto system = std::make_unique<System>();
        test::initialize_memory(*system);
        prepare_copy(*system, 0, true, 0, 0x200, 32, 0x50);
        start_dma(*system, 0, true, 0, 0x200, 31);

        advance_cpu(*system, 5, split); // Three RCP cycles from the initial phase.
        CHECK_EQ(read_register(*system, sp_dma_busy), 1U);
        check_destination_clear(*system, 0, true, 0, 0x200, 32);

        advance_cpu(*system, 1, split); // Four RCP cycles total.
        CHECK_EQ(read_register(*system, sp_dma_busy), 0U);
        check_destination(*system, 0, true, 0, 0x200, 32, 0x50);
    }
}

TEST(rsp_dma_timing_rsp_started_transfer_is_visible_to_next_memory_load) {
    for (const bool vector_load : {false, true}) {
        auto system = std::make_unique<System>();
        test::initialize_memory(*system);
        system->bus.write(0x04000000U, 4, 0x11112222U);
        system->bus.write(0x04000020U, 4, 0xeeeeeeeeU);
        system->bus.memory.write(0x200, 8, 0xa0a1a2a3a4a5a6a7ULL);

        instruction(system->rsp, 0x00, 0x24010007U); // ADDIU r1,zero,7.
        instruction(system->rsp, 0x04, 0x40811000U); // MTC0 r1,SP_RD_LEN.
        if (vector_load) {
            instruction(system->rsp, 0x08, 0xc8010000U); // LBV v1[0],0(zero).
            instruction(system->rsp, 0x0c, 0xe8010020U); // SBV v1[0],0x20(zero).
        } else {
            instruction(system->rsp, 0x08, 0x8c020000U); // LW r2,0(zero).
            instruction(system->rsp, 0x0c, 0xac020020U); // SW r2,0x20(zero).
        }
        instruction(system->rsp, 0x10, 0x0000000dU); // BREAK.

        write_register(*system, sp_mem_addr, 0);
        write_register(*system, sp_dram_addr, 0x200);
        write_register(*system, sp_status, 1);
        system->rsp.tick(16);

        CHECK_EQ(read_register(*system, sp_dma_busy), 0U);
        if (vector_load)
            CHECK_EQ(system->rsp.memory[0x20], 0xa0U);
        else
            CHECK_EQ(system->bus.read(0x04000020U, 4), 0xa0a1a2a3U);
    }
}

TEST(rsp_dma_timing_progresses_during_operand_stalls) {
    auto system = std::make_unique<System>();
    test::initialize_memory(*system);
    system->bus.write(0x04000000U, 4, 0x01020304U);
    prepare_copy(*system, 0, true, 0x100, 0x200, 16, 0x70);
    instruction(system->rsp, 0x00, 0x8c010000U); // LW r1,0(zero).
    instruction(system->rsp, 0x04, 0x00211021U); // ADDU r2,r1,r1.
    instruction(system->rsp, 0x08, 0x0000000dU); // BREAK.

    start_dma(*system, 0, true, 0x100, 0x200, 15);
    write_register(*system, sp_status, 1);
    system->rsp.tick(1);
    CHECK_EQ(system->rsp.pc, 4U);
    CHECK_EQ(read_register(*system, sp_dma_busy), 1U);

    system->rsp.tick(1);
    CHECK_EQ(system->rsp.pc, 4U); // First load-use stall.
    CHECK_EQ(read_register(*system, sp_dma_busy), 0U);
    check_destination(*system, 0, true, 0x100, 0x200, 16, 0x70);
}
