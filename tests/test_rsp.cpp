#include "test.hpp"
#include "test_system.hpp"

#include "cupid/system.hpp"

#include <array>
#include <cstddef>
#include <memory>

using namespace cupid;

namespace {

constexpr u32 addiu(unsigned rt, unsigned rs, s16 immediate) {
    return (0x09u << 26) | (rs << 21) | (rt << 16) | static_cast<u16>(immediate);
}

constexpr u32 beq(unsigned rs, unsigned rt, s16 immediate) {
    return (0x04u << 26) | (rs << 21) | (rt << 16) | static_cast<u16>(immediate);
}

constexpr u32 sw(unsigned rt, unsigned rs, s16 immediate) {
    return (0x2bu << 26) | (rs << 21) | (rt << 16) | static_cast<u16>(immediate);
}

constexpr u32 break_instruction() {
    return 0x0000'000d;
}

constexpr u32 cop2_control(bool write, unsigned rt, unsigned rd) {
    return (0x12u << 26) | ((write ? 0x06u : 0x02u) << 21) | (rt << 16) | (rd << 11);
}

constexpr u32 cop0_move(bool write, unsigned rt, unsigned rd) {
    return (0x10u << 26) | ((write ? 0x04u : 0x00u) << 21) | (rt << 16) | (rd << 11);
}

constexpr u32 vector_op(unsigned function, unsigned vd, unsigned vs, unsigned vt, unsigned element) {
    return (0x12u << 26) | ((0x10u | (element & 15u)) << 21) | (vt << 16) | (vs << 11) | (vd << 6) |
           (function & 63u);
}

constexpr u32 vector_load(unsigned kind, unsigned vt, unsigned element, s8 immediate, unsigned base) {
    return (0x32u << 26) | (base << 21) | (vt << 16) | (kind << 11) | ((element & 15u) << 7) |
           (static_cast<u8>(immediate) & 0x7fu);
}

constexpr u32 vector_store(unsigned kind, unsigned vt, unsigned element, s8 immediate, unsigned base) {
    return (0x3au << 26) | (base << 21) | (vt << 16) | (kind << 11) | ((element & 15u) << 7) |
           (static_cast<u8>(immediate) & 0x7fu);
}

void put_instruction(Rsp& rsp, u32 address, u32 instruction) {
    write_be32(&rsp.memory[0x1000u | (address & 0xffcu)], instruction);
}

u32 dmem_word(const Rsp& rsp, u32 address) {
    const u32 a = address & 0xfffu;
    return (static_cast<u32>(rsp.memory[a]) << 24) | (static_cast<u32>(rsp.memory[(a + 1) & 0xfff]) << 16) |
           (static_cast<u32>(rsp.memory[(a + 2) & 0xfff]) << 8) |
           static_cast<u32>(rsp.memory[(a + 3) & 0xfff]);
}

u16 dmem_half(const Rsp& rsp, u32 address) {
    const u32 a = address & 0xfffu;
    return static_cast<u16>((static_cast<u16>(rsp.memory[a]) << 8) | rsp.memory[(a + 1) & 0xfff]);
}

void run_rsp(System& system, u32 start = 0, u64 cycles = 256) {
    system.rsp.pc = start & 0xffc;
    system.rsp.write_register(0x10, 1u << 0);
    system.rsp.tick(cycles);
    CHECK((system.rsp.read_register(0x10) & 1u) != 0);
}

void write_vector_data(Rsp& rsp, u32 address, const std::array<u16, 8>& values) {
    for (unsigned lane = 0; lane < values.size(); ++lane) {
        const u32 offset = (address + lane * 2) & 0xfff;
        rsp.memory[offset] = static_cast<u8>(values[lane] >> 8);
        rsp.memory[(offset + 1) & 0xfff] = static_cast<u8>(values[lane]);
    }
}

} // namespace

TEST(rsp_scalar_branch_wrap_and_delay_slot) {
    System system;

    put_instruction(system.rsp, 0x0ff8, addiu(1, 0, 1));
    put_instruction(system.rsp, 0x0ffc, beq(1, 1, 2));
    put_instruction(system.rsp, 0x0000, addiu(2, 0, 0x1234));
    put_instruction(system.rsp, 0x0004, addiu(2, 0, 0x5555));
    put_instruction(system.rsp, 0x000c, sw(2, 0, 0));
    put_instruction(system.rsp, 0x0010, break_instruction());

    run_rsp(system, 0x0ff8);

    CHECK_EQ(dmem_word(system.rsp, 0), 0x0000'1234u);
    CHECK_EQ(system.rsp.pc, 0x14u);
    CHECK((system.rsp.read_register(0x10) & (1u << 1)) != 0);
}

TEST(rsp_status_pairs_and_semaphore) {
    System system;

    system.rsp.write_register(0x1c, 0xffff'ffff);
    CHECK_EQ(system.rsp.read_register(0x1c), 0u);
    CHECK_EQ(system.rsp.read_register(0x1c), 1u);
    CHECK_EQ(system.rsp.read_register(0x1c), 1u);

    system.rsp.write_register(0x10, 1u << 10);
    CHECK((system.rsp.read_register(0x10) & (1u << 7)) != 0);
    system.rsp.write_register(0x10, (1u << 9) | (1u << 10));
    CHECK((system.rsp.read_register(0x10) & (1u << 7)) != 0);
    system.rsp.write_register(0x10, 1u << 9);
    CHECK((system.rsp.read_register(0x10) & (1u << 7)) == 0);

    CHECK((system.rsp.read_register(0x10) & 1u) != 0);
    system.rsp.write_register(0x10, (1u << 0) | (1u << 1));
    CHECK((system.rsp.read_register(0x10) & 1u) != 0);
}

TEST(rsp_cop0_register_aliases_decode_rd_bit_three) {
    {
        auto system = std::make_unique<System>();
        put_instruction(system->rsp, 0x0000, cop0_move(false, 1, 23));
        put_instruction(system->rsp, 0x0004, break_instruction());
        run_rsp(*system);
        CHECK_EQ(system->rsp.read_register(0x1c), 1u);
    }

    {
        auto system = std::make_unique<System>();
        CHECK_EQ(system->rsp.read_register(0x1c), 0u);
        put_instruction(system->rsp, 0x0000, cop0_move(true, 0, 23));
        put_instruction(system->rsp, 0x0004, break_instruction());
        run_rsp(*system);
        CHECK_EQ(system->rsp.read_register(0x1c), 0u);
    }

    {
        auto system = std::make_unique<System>();
        put_instruction(system->rsp, 0x0000, addiu(1, 0, 1 << 10));
        put_instruction(system->rsp, 0x0004, cop0_move(true, 1, 20));
        put_instruction(system->rsp, 0x0008, break_instruction());
        run_rsp(*system);
        CHECK((system->rsp.read_register(0x10) & (1u << 7)) != 0);
    }

    {
        auto system = std::make_unique<System>();
        system->bus.rdp.write_register(0x0c, 1u << 3);
        put_instruction(system->rsp, 0x0000, cop0_move(false, 1, 27));
        put_instruction(system->rsp, 0x0004, sw(1, 0, 0x100));
        put_instruction(system->rsp, 0x0008, break_instruction());
        run_rsp(*system);
        CHECK((dmem_word(system->rsp, 0x100) & (1u << 1)) != 0);
    }

    {
        auto system = std::make_unique<System>();
        put_instruction(system->rsp, 0x0000, addiu(1, 0, 1 << 3));
        put_instruction(system->rsp, 0x0004, cop0_move(true, 1, 27));
        put_instruction(system->rsp, 0x0008, break_instruction());
        run_rsp(*system);
        CHECK((system->bus.rdp.read_register(0x0c) & (1u << 1)) != 0);
    }
}

TEST(rsp_vector_add_flags_and_saturation) {
    System system;
    write_vector_data(system.rsp, 0x100,
                      {
                          0xffff,
                          0x0001,
                          0x7fff,
                          0x8000,
                          0x1234,
                          0xfffe,
                          0x0100,
                          0x0000,
                      });
    write_vector_data(system.rsp, 0x110,
                      {
                          0x0001,
                          0xffff,
                          0x0001,
                          0x8000,
                          0x1111,
                          0x0002,
                          0xff00,
                          0x0000,
                      });

    put_instruction(system.rsp, 0x00, addiu(1, 0, 0x100));
    put_instruction(system.rsp, 0x04, vector_load(0x04, 1, 0, 0, 1));
    put_instruction(system.rsp, 0x08, vector_load(0x04, 2, 0, 1, 1));
    put_instruction(system.rsp, 0x0c, vector_op(0x14, 3, 1, 2, 0));
    put_instruction(system.rsp, 0x10, cop2_control(false, 2, 0));
    put_instruction(system.rsp, 0x14, sw(2, 0, 0x140));
    put_instruction(system.rsp, 0x18, vector_store(0x04, 3, 0, 2, 1));
    put_instruction(system.rsp, 0x1c, vector_op(0x10, 4, 1, 2, 0));
    put_instruction(system.rsp, 0x20, vector_store(0x04, 4, 0, 3, 1));
    put_instruction(system.rsp, 0x24, cop2_control(false, 3, 0));
    put_instruction(system.rsp, 0x28, sw(3, 0, 0x144));
    put_instruction(system.rsp, 0x2c, break_instruction());

    run_rsp(system);

    CHECK_EQ(dmem_word(system.rsp, 0x140), 0x0000'006bu);
    CHECK_EQ(dmem_word(system.rsp, 0x144), 0u);
    CHECK_EQ(dmem_half(system.rsp, 0x120), 0x0000u);
    CHECK_EQ(dmem_half(system.rsp, 0x122), 0x0000u);
    CHECK_EQ(dmem_half(system.rsp, 0x124), 0x8000u);
    CHECK_EQ(dmem_half(system.rsp, 0x126), 0x0000u);
    CHECK_EQ(dmem_half(system.rsp, 0x130), 0x0001u);
    CHECK_EQ(dmem_half(system.rsp, 0x132), 0x0001u);
    CHECK_EQ(dmem_half(system.rsp, 0x134), 0x7fffu);
    CHECK_EQ(dmem_half(system.rsp, 0x136), 0x8000u);
}

TEST(rsp_vector_accumulator_slices) {
    System system;
    write_vector_data(system.rsp, 0x100,
                      {
                          0x4000,
                          0x4000,
                          0x4000,
                          0x4000,
                          0x4000,
                          0x4000,
                          0x4000,
                          0x4000,
                      });

    put_instruction(system.rsp, 0x00, addiu(1, 0, 0x100));
    put_instruction(system.rsp, 0x04, vector_load(0x04, 1, 0, 0, 1));
    put_instruction(system.rsp, 0x08, vector_op(0x00, 2, 1, 1, 0));
    put_instruction(system.rsp, 0x0c, vector_op(0x1d, 3, 0, 0, 8));
    put_instruction(system.rsp, 0x10, vector_op(0x1d, 4, 0, 0, 9));
    put_instruction(system.rsp, 0x14, vector_op(0x1d, 5, 0, 0, 10));
    put_instruction(system.rsp, 0x18, vector_store(0x04, 2, 0, 1, 1));
    put_instruction(system.rsp, 0x1c, vector_store(0x04, 3, 0, 2, 1));
    put_instruction(system.rsp, 0x20, vector_store(0x04, 4, 0, 3, 1));
    put_instruction(system.rsp, 0x24, vector_store(0x04, 5, 0, 4, 1));
    put_instruction(system.rsp, 0x28, break_instruction());

    run_rsp(system);

    CHECK_EQ(dmem_half(system.rsp, 0x110), 0x2000u);
    CHECK_EQ(dmem_half(system.rsp, 0x120), 0x0000u);
    CHECK_EQ(dmem_half(system.rsp, 0x130), 0x2000u);
    CHECK_EQ(dmem_half(system.rsp, 0x140), 0x8000u);
}

TEST(rsp_vector_load_store_alignment_boundaries) {
    System system;
    for (u32 index = 0; index < 32; ++index) {
        system.rsp.memory[0x100 + index] = static_cast<u8>(0x80 + index);
    }

    put_instruction(system.rsp, 0x00, addiu(1, 0, 0x10b));
    put_instruction(system.rsp, 0x04, vector_load(0x04, 1, 3, 0, 1));
    put_instruction(system.rsp, 0x08, addiu(2, 0, 0x200));
    put_instruction(system.rsp, 0x0c, vector_store(0x04, 1, 0, 0, 2));
    put_instruction(system.rsp, 0x10, addiu(1, 0, 0x11b));
    put_instruction(system.rsp, 0x14, vector_load(0x05, 1, 3, 0, 1));
    put_instruction(system.rsp, 0x18, vector_store(0x04, 1, 0, 1, 2));
    put_instruction(system.rsp, 0x1c, break_instruction());

    run_rsp(system);

    CHECK_EQ(system.rsp.memory[0x203], 0x8bu);
    CHECK_EQ(system.rsp.memory[0x204], 0x8cu);
    CHECK_EQ(system.rsp.memory[0x205], 0x8du);
    CHECK_EQ(system.rsp.memory[0x206], 0x8eu);
    CHECK_EQ(system.rsp.memory[0x207], 0x8fu);
    CHECK_EQ(system.rsp.memory[0x213], 0x8bu);
    CHECK_EQ(system.rsp.memory[0x217], 0x8fu);
    CHECK_EQ(system.rsp.memory[0x218], 0x90u);
    CHECK_EQ(system.rsp.memory[0x21f], 0x97u);
}

TEST(rsp_reciprocal_low_and_high_result) {
    System system;
    write_vector_data(system.rsp, 0x100, {1, 0, 0, 0, 0, 0, 0, 0});

    put_instruction(system.rsp, 0x00, addiu(1, 0, 0x100));
    put_instruction(system.rsp, 0x04, vector_load(0x04, 1, 0, 0, 1));
    put_instruction(system.rsp, 0x08, vector_op(0x30, 2, 0, 1, 8));
    put_instruction(system.rsp, 0x0c, vector_op(0x32, 3, 1, 1, 8));
    put_instruction(system.rsp, 0x10, addiu(2, 0, 0x200));
    put_instruction(system.rsp, 0x14, vector_store(0x04, 2, 0, 0, 2));
    put_instruction(system.rsp, 0x18, vector_store(0x04, 3, 0, 1, 2));
    put_instruction(system.rsp, 0x1c, break_instruction());

    run_rsp(system);

    CHECK_EQ(dmem_half(system.rsp, 0x200), 0xc000u);
    CHECK_EQ(dmem_half(system.rsp, 0x212), 0x7fffu);
}

TEST(rsp_divider_uses_raw_element_when_registers_alias) {
    System system;
    write_vector_data(system.rsp, 0x100, {0, 1, 2, 3, 4, 5, 6, 7});

    put_instruction(system.rsp, 0x00, addiu(1, 0, 0x100));
    put_instruction(system.rsp, 0x04, vector_load(0x04, 0, 0, 0, 1));
    put_instruction(system.rsp, 0x08, vector_op(0x30, 0, 0, 0, 1));
    put_instruction(system.rsp, 0x0c, addiu(2, 0, 0x200));
    put_instruction(system.rsp, 0x10, vector_store(0x04, 0, 0, 0, 2));
    put_instruction(system.rsp, 0x14, break_instruction());

    run_rsp(system);

    CHECK_EQ(dmem_half(system.rsp, 0x200), 0xc000u);
    CHECK_EQ(dmem_half(system.rsp, 0x202), 0x0001u);
    CHECK_EQ(dmem_half(system.rsp, 0x20e), 0x0007u);
}

TEST(rsp_dma_alignment_wrap_queue_and_postincrement) {
    System system;
    test::initialize_memory(system);
    for (u32 index = 0; index < 0x80; index += 8) {
        u64 value = 0;
        for (u32 byte = 0; byte < 8; ++byte) {
            value = (value << 8) | static_cast<u8>(index + byte + 1);
        }
        system.bus.memory.write(0x20 + index, 8, value);
    }
    u64 queued_value = 0;
    for (u32 byte = 0; byte < 8; ++byte) {
        queued_value = (queued_value << 8) | static_cast<u8>(0xa0 + byte);
    }
    system.bus.memory.write(0x100, 8, queued_value);

    system.rsp.write_register(0x00, 0x0fff);
    system.rsp.write_register(0x04, 0x0023);
    system.rsp.write_register(0x08, 0x0080'100f);
    CHECK_EQ(system.rsp.read_register(0x18), 1u);
    CHECK_EQ(system.rsp.read_register(0x14), 0u);

    system.rsp.write_register(0x00, 0x1007);
    system.rsp.write_register(0x04, 0x0107);
    system.rsp.write_register(0x08, 0x0000'0007);
    CHECK_EQ(system.rsp.read_register(0x14), 1u);

    system.rsp.tick(4);

    CHECK_EQ(system.rsp.memory[0x0ff8], 0x01u);
    CHECK_EQ(system.rsp.memory[0x0fff], 0x08u);
    CHECK_EQ(system.rsp.memory[0x0000], 0x09u);
    CHECK_EQ(system.rsp.memory[0x0007], 0x10u);
    CHECK_EQ(system.rsp.memory[0x0008], 0x19u);
    CHECK_EQ(system.rsp.memory[0x0017], 0x28u);
    CHECK_EQ(system.rsp.read_register(0x18), 1u);
    CHECK_EQ(system.rsp.read_register(0x14), 0u);
    CHECK_EQ(system.rsp.read_register(0x00), 0x1000u);
    CHECK_EQ(system.rsp.read_register(0x04), 0x0100u);

    system.rsp.tick(1);

    for (u32 index = 0; index < 8; ++index) {
        CHECK_EQ(system.rsp.memory[0x1000 + index], static_cast<u8>(0xa0 + index));
    }
    CHECK_EQ(system.rsp.read_register(0x18), 0u);
    CHECK_EQ(system.rsp.read_register(0x00), 0x1008u);
    CHECK_EQ(system.rsp.read_register(0x04), 0x0108u);
    CHECK_EQ(system.rsp.read_register(0x08) & 0xfffu, 0x0ff8u);
}

TEST(rsp_dma_sp_to_rdram_wraps_inside_selected_bank) {
    System system;
    test::initialize_memory(system);
    for (u32 index = 0; index < 8; ++index) {
        system.rsp.memory[0x1ff8 + index] = static_cast<u8>(0x40 + index);
        system.rsp.memory[0x1000 + index] = static_cast<u8>(0x48 + index);
    }

    system.rsp.write_register(0x00, 0x1ff8);
    system.rsp.write_register(0x04, 0x0200);
    system.rsp.write_register(0x0c, 0x0000'000f);
    system.rsp.tick(2);

    for (u32 index = 0; index < 16; ++index) {
        CHECK_EQ(system.bus.read_ram_byte(0x200 + index), static_cast<u8>(0x40 + index));
    }
    CHECK_EQ(system.rsp.read_register(0x00), 0x1008u);
    CHECK_EQ(system.rsp.read_register(0x04), 0x0210u);
}
