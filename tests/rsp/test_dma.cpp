#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>

namespace {
using namespace cupid;

constexpr u32 sp_registers = 0x04040000;

void write_register(System& system, u32 offset, u32 value) {
    system.bus.write(sp_registers + offset, 4, value);
}

u32 read_register(System& system, u32 offset) {
    return static_cast<u32>(system.bus.read(sp_registers + offset, 4));
}

void fill_sources(System& system, u32 bank, bool to_sp) {
    for (u32 offset = 0; offset < 0x100; ++offset) {
        system.rsp.memory[bank + offset] = to_sp ? 0 : static_cast<u8>(offset + 1);
        system.bus.write_ram_byte(0x200 + offset, to_sp ? static_cast<u8>(offset + 1) : 0);
    }
}

void check_copy(System& system, u32 bank, bool to_sp, u32 sp, u32 dram, u8 expected) {
    for (u32 byte = 0; byte < 8; ++byte) {
        const u8 actual = to_sp ? system.rsp.memory[bank + sp + byte] : system.bus.read_ram_byte(dram + byte);
        CHECK_EQ(actual, static_cast<u8>(expected + byte));
    }
}

void advance_cpu(System& system, u64 cycles, bool single) {
    if (single) {
        for (u64 cycle = 0; cycle < cycles; ++cycle)
            system.advance(1);
    } else {
        system.advance(cycles);
    }
}
} // namespace

TEST(rsp_dma_length_only_start_reuses_programmed_addresses) {
    for (const bool to_sp : {false, true}) {
        for (const u32 bank : {0U, 0x1000U}) {
            auto system = std::make_unique<System>();
            test::initialize_memory(*system);
            fill_sources(*system, bank, to_sp);
            const u32 length_register = to_sp ? 0x08U : 0x0cU;
            write_register(*system, 0x00, bank + 7);
            write_register(*system, 0x04, 0x207);
            write_register(*system, length_register, 7);
            system->rsp.tick(3);
            CHECK_EQ(read_register(*system, 0x00), bank + 8);
            CHECK_EQ(read_register(*system, 0x04), 0x208U);
            check_copy(*system, bank, to_sp, 0, 0x200, 1);

            if (to_sp)
                system->bus.write_ram_byte(0x200, 0xa5);
            else
                system->rsp.memory[bank] = 0xa5;
            write_register(*system, length_register, 7);
            CHECK_EQ(read_register(*system, 0x00), bank);
            CHECK_EQ(read_register(*system, 0x04), 0x200U);
            system->rsp.tick(3);
            CHECK_EQ(to_sp ? system->rsp.memory[bank] : system->bus.read_ram_byte(0x200), 0xa5U);
            CHECK_EQ(to_sp ? system->rsp.memory[bank + 8] : system->bus.read_ram_byte(0x208), 0U);
            CHECK_EQ(read_register(*system, 0x18), 0U);
        }
    }
}

TEST(rsp_dma_completion_preserves_addresses_staged_while_busy) {
    for (const bool to_sp : {false, true}) {
        for (const u32 bank : {0U, 0x1000U}) {
            auto system = std::make_unique<System>();
            test::initialize_memory(*system);
            fill_sources(*system, bank, to_sp);
            const u32 length_register = to_sp ? 0x08U : 0x0cU;
            write_register(*system, 0x00, bank);
            write_register(*system, 0x04, 0x200);
            write_register(*system, length_register, 7);
            system->rsp.tick(1);
            write_register(*system, 0x00, bank + 0x47);
            write_register(*system, 0x04, 0x247);
            CHECK_EQ(read_register(*system, 0x00), bank);
            CHECK_EQ(read_register(*system, 0x04), 0x200U);
            CHECK_EQ(read_register(*system, 0x14), 0U);
            system->rsp.tick(2);
            check_copy(*system, bank, to_sp, 0, 0x200, 1);
            CHECK_EQ(read_register(*system, 0x00), bank + 8);
            CHECK_EQ(read_register(*system, 0x04), 0x208U);

            write_register(*system, length_register, 7);
            CHECK_EQ(read_register(*system, 0x00), bank + 0x40);
            CHECK_EQ(read_register(*system, 0x04), 0x240U);
            system->rsp.tick(3);
            check_copy(*system, bank, to_sp, 0x40, 0x240, 0x41);
        }
    }
}

TEST(rsp_dma_queued_transfer_samples_addresses_when_promoted) {
    for (const bool to_sp : {false, true}) {
        for (const u32 bank : {0U, 0x1000U}) {
            auto system = std::make_unique<System>();
            test::initialize_memory(*system);
            fill_sources(*system, bank, to_sp);
            const u32 length_register = to_sp ? 0x08U : 0x0cU;
            write_register(*system, 0x00, bank);
            write_register(*system, 0x04, 0x200);
            write_register(*system, length_register, 7);
            write_register(*system, 0x00, bank + 0x20);
            write_register(*system, 0x04, 0x220);
            write_register(*system, length_register, 7);
            CHECK_EQ(read_register(*system, 0x14), 1U);
            write_register(*system, 0x00, bank + 0x47);
            write_register(*system, 0x04, 0x247);
            CHECK_EQ(read_register(*system, 0x00), bank);
            CHECK_EQ(read_register(*system, 0x04), 0x200U);

            system->rsp.tick(3);
            check_copy(*system, bank, to_sp, 0, 0x200, 1);
            CHECK_EQ(read_register(*system, 0x14), 0U);
            CHECK_EQ(read_register(*system, 0x18), 1U);
            CHECK_EQ(read_register(*system, 0x00), bank + 0x40);
            CHECK_EQ(read_register(*system, 0x04), 0x240U);
            system->rsp.tick(3);
            check_copy(*system, bank, to_sp, 0x40, 0x240, 0x41);
            CHECK_EQ(to_sp ? system->rsp.memory[bank + 0x20] : system->bus.read_ram_byte(0x220), 0U);
            CHECK_EQ(read_register(*system, 0x18), 0U);
        }
    }
}

TEST(rsp_dma_rsp_cop0_staging_survives_completion) {
    for (const bool to_sp : {false, true}) {
        for (const bool single : {false, true}) {
            auto system = std::make_unique<System>();
            test::initialize_memory(*system);
            fill_sources(*system, 0, to_sp);
            const u32 length_register = to_sp ? 0x08U : 0x0cU;
            const std::array<u32, 12> program = {
                0x24010040, // ADDIU at, zero, 0x40
                0x40810000, // MTC0 at, SP_MEM_ADDR
                0x24010240, // ADDIU at, zero, 0x240
                0x40810800, // MTC0 at, SP_DRAM_ADDR
                0x40020000, // MFC0 v0, SP_MEM_ADDR
                0xac020080, // SW v0, 0x80(zero)
                0x0000000d, // BREAK before the active transfer finishes
                0x40020000, // MFC0 v0, SP_MEM_ADDR after completion
                0xac020084, // SW v0, 0x84(zero)
                0x24010007, // ADDIU at, zero, 7
                0x40810000U | ((length_register >> 2U) << 11U),
                0x0000000d,
            };
            for (u32 index = 0; index < program.size(); ++index)
                system->bus.write(0x04001000U + index * 4, 4, program[index]);
            write_register(*system, 0x00, 0);
            write_register(*system, 0x04, 0x200);
            write_register(*system, length_register, 31);
            write_register(*system, 0x10, 1);
            advance_cpu(*system, 18, single);
            CHECK_EQ(system->bus.read(0x04000080, 4), 0U);
            CHECK_EQ(read_register(*system, 0x18), 0U);
            CHECK_EQ(read_register(*system, 0x00), 0x20U);
            CHECK_EQ(read_register(*system, 0x04), 0x220U);
            CHECK_EQ(read_register(*system, 0x10) & 3U, 3U);

            write_register(*system, 0x10, 5);
            advance_cpu(*system, 12, single);
            CHECK_EQ(system->bus.read(0x04000084, 4), 0x20U);
            check_copy(*system, 0, to_sp, 0x40, 0x240, 0x41);
            CHECK_EQ(read_register(*system, 0x18), 0U);
            CHECK_EQ(read_register(*system, 0x00), 0x48U);
            CHECK_EQ(read_register(*system, 0x04), 0x248U);
            CHECK_EQ(read_register(*system, 0x10) & 3U, 3U);
        }
    }
}
