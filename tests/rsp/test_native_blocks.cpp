#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>
#include <vector>

using namespace cupid;

namespace {

constexpr u32 immediate(unsigned opcode, unsigned rs, unsigned rt, int value) {
    return (opcode << 26U) | (rs << 21U) | (rt << 16U) | static_cast<u16>(value);
}

constexpr u32 special(unsigned function, unsigned rs, unsigned rt, unsigned rd, unsigned shift = 0) {
    return (rs << 21U) | (rt << 16U) | (rd << 11U) | (shift << 6U) | function;
}

void install(System& system, std::span<const u32> body, u32 start = 0) {
    std::vector<u32> program;
    for (unsigned reg = 1; reg < 32; ++reg) {
        const u32 value = 0x80007fffU * reg;
        program.push_back(immediate(15, 0, reg, static_cast<int>(value >> 16U)));
        program.push_back(immediate(13, reg, reg, static_cast<int>(value & 0xffffU)));
    }
    const auto loop = static_cast<u32>((start + program.size() * 4U) & 0xfffU);
    program.insert(program.end(), body.begin(), body.end());
    for (unsigned reg = 0; reg < 32; ++reg)
        program.push_back(immediate(0x2b, 0, reg, static_cast<int>(0x100U + reg * 4U)));
    program.push_back(0x08000000U | (loop >> 2U));
    program.push_back(0);
    for (unsigned index = 0; index < program.size(); ++index)
        system.bus.write(0x04001000U + ((start + index * 4U) & 0xfffU), 4, program[index]);
    for (u32 offset = 0; offset < 4096; offset += 4)
        system.bus.write(0x04000000U + offset, 4, 0x80ff7f01U ^ (offset * 0x10203U));
    system.rsp.write_pc(start);
    system.rsp.write_register(0x10, 1);
}

void compare(System& native, System& ordinary, u64 cycles) {
    native.advance(cycles);
    ordinary.advance(cycles);
    native.settle();
    ordinary.settle();
    CHECK_EQ(native.rsp.pc, ordinary.rsp.pc);
    CHECK_EQ(native.rsp.read_register(0x10), ordinary.rsp.read_register(0x10));
    CHECK_EQ(native.bus.output_clock(), ordinary.bus.output_clock());
    for (u32 address = 0; address < 4096; address += 4)
        CHECK_EQ(native.bus.read(0x04000000U + address, 4), ordinary.bus.read(0x04000000U + address, 4));
}

void require_native(const System& machine) {
    CHECK_EQ(machine.rsp.native_block_instructions() != 0, RspNativeCode::available());
}

constexpr std::array scalar_body{
    special(0x00, 0, 1, 3, 31),    special(0x02, 0, 2, 4, 17),     special(0x03, 0, 1, 5, 31),
    special(0x04, 2, 3, 3),        special(0x06, 2, 4, 4),         special(0x07, 1, 2, 5),
    special(0x21, 1, 2, 6),        special(0x23, 6, 2, 6),         special(0x24, 1, 6, 7),
    special(0x25, 3, 6, 8),        special(0x26, 4, 8, 9),         special(0x27, 1, 9, 10),
    special(0x2a, 1, 10, 11),      special(0x2b, 1, 10, 12),       special(0x01, 1, 2, 13),
    immediate(9, 1, 14, -32768),   immediate(0x0a, 1, 15, -1),     immediate(0x0b, 1, 16, -1),
    immediate(0x0c, 2, 17, 65535), immediate(0x0d, 3, 18, 0x8080), immediate(0x0e, 4, 19, 65535),
    immediate(15, 0, 20, 0x8000),  immediate(9, 20, 20, -1),       special(0x21, 1, 2, 0),
    special(0x25, 0, 0, 21),       immediate(9, 0, 0, -1),
};

} // namespace

TEST(rsp_native_blocks_match_scalar_results_aliases_and_partial_cycle_budgets) {
    for (unsigned phase = 0; phase < 3; ++phase) {
        auto machines = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *machines;
        ordinary.rsp.set_native_execution(false);
        for (auto* machine : {&native, &ordinary}) {
            machine->advance(phase);
            install(*machine, scalar_body);
        }
        for (u64 cycles : {4096U, 1U, 2U, 3U, 17U, 63U, 96U, 257U, 2048U})
            compare(native, ordinary, cycles);
        require_native(native);
        CHECK_EQ(ordinary.rsp.native_block_instructions(), 0U);
        CHECK_EQ(native.bus.read(0x04000100U, 4), 0U);
    }
}

TEST(rsp_native_blocks_keep_dmem_byte_order_sign_extension_and_wrapping) {
    for (int address : {0, 1, 0xffc, 0xffd, 0xffe, 0xfff, -1}) {
        const std::array body{
            immediate(9, 0, 1, address), immediate(15, 0, 2, 0x80ff), immediate(13, 2, 2, 0x7f01),
            immediate(0x2b, 1, 2, 0),    immediate(0x20, 1, 3, 0),    immediate(0x24, 1, 4, 1),
            immediate(0x21, 1, 5, 0),    immediate(0x25, 1, 6, 1),    immediate(0x23, 1, 7, 0),
            immediate(0x28, 1, 2, -2),   immediate(0x29, 1, 2, -1),   immediate(0x23, 1, 1, 0),
            immediate(0x23, 1, 0, 0),
        };
        auto machines = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *machines;
        ordinary.rsp.set_native_execution(false);
        for (auto* machine : {&native, &ordinary})
            install(*machine, body);
        for (u64 cycles : {4096U, 127U, 1U, 2U, 3U, 256U})
            compare(native, ordinary, cycles);
        require_native(native);
        CHECK_EQ(native.bus.read(0x0400010cU, 4), 0xffffff80U);
        CHECK_EQ(native.bus.read(0x04000114U, 4), 0xffff80ffU);
        CHECK_EQ(native.bus.read(0x0400011cU, 4), 0x80ff7f01U);
    }
}

TEST(rsp_native_blocks_keep_lwu_alias_at_each_dmem_wrap_boundary) {
    for (int address : {0xffc, 0xffd, 0xffe, 0xfff}) {
        const std::array body{
            immediate(9, 0, 1, address), immediate(15, 0, 2, 0x80ff), immediate(13, 2, 2, 0x7f01),
            immediate(0x2b, 1, 2, 0),    immediate(0x27, 1, 3, 0),    immediate(0x27, 1, 1, 0),
        };
        auto machines = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *machines;
        ordinary.rsp.set_native_execution(false);
        for (auto* machine : {&native, &ordinary})
            install(*machine, body);
        compare(native, ordinary, 8192);
        require_native(native);
        CHECK_EQ(native.bus.read(0x0400010cU, 4), 0x80ff7f01U);
        CHECK_EQ(native.bus.read(0x04000104U, 4), 0x80ff7f01U);
    }
}

TEST(rsp_native_blocks_share_vector_helpers_and_accumulator_state) {
    constexpr std::array body{
        0xc8012040U, 0xc8022041U,              // LQV v1/2 from 0x400/0x410.
        0x4a0208c0U, 0x4a02390fU,              // VMULF and VMADH.
        0x4a021950U, 0x4a021995U,              // VADD and VSUBC.
        0x4a021925U, 0x4a021924U,              // VCH and VCL.
        0x4b001a1dU, 0x4b201a5dU, 0x4b401a9dU, // VSAW high/middle/low.
        0xe8032020U, 0xe8042021U, 0xe8052022U, 0xe8062023U, 0xe8082024U,
        0xe8092025U, 0xe80a2026U, 0x484b0000U, 0x484c0800U, 0x484d1000U, // CFC2 flags.
    };
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *machines;
    ordinary.rsp.set_native_execution(false);
    for (auto* machine : {&native, &ordinary})
        install(*machine, body);
    for (u64 cycles : {8192U, 1U, 31U, 256U, 4096U})
        compare(native, ordinary, cycles);
    require_native(native);
}

TEST(rsp_native_blocks_match_every_vector_opcode_and_element_with_aliased_destinations) {
    for (unsigned function = 0; function < 64; ++function) {
        for (unsigned element = 0; element < 16; ++element) {
            const unsigned destination = 1U + element % 3U;
            const u32 operation =
                0x4a000000U | (element << 21U) | (2U << 16U) | (1U << 11U) | (destination << 6U) | function;
            const std::array body{
                0xc8012040U, 0xc8022041U, operation,   0xe8012020U, 0xe8022021U,
                0xe8032022U, 0x4b001a1dU, 0x4b201a5dU, 0x4b401a9dU, 0xe8082024U,
                0xe8092025U, 0xe80a2026U, 0x484b0000U, 0x484c0800U, 0x484d1000U,
            };
            auto machines = std::make_unique<std::array<System, 2>>();
            auto& [native, ordinary] = *machines;
            ordinary.rsp.set_native_execution(false);
            for (auto* machine : {&native, &ordinary})
                install(*machine, body);
            compare(native, ordinary, 1536);
            require_native(native);
        }
    }
}

TEST(rsp_native_blocks_invalidate_interior_code_and_preserve_imem_wrap) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *machines;
    ordinary.rsp.set_native_execution(false);
    for (auto* machine : {&native, &ordinary})
        install(*machine, scalar_body, 0xf00U);
    compare(native, ordinary, 8192);
    require_native(native);
    for (auto* machine : {&native, &ordinary}) {
        machine->bus.write(0x04001000U, 4, immediate(9, 0, 4, 0x321));
        machine->rsp.write_pc(0xff8U);
    }
    for (u64 cycles : {4096U, 1U, 97U, 513U})
        compare(native, ordinary, cycles);
}

TEST(rsp_native_blocks_stop_before_dma_visibility) {
    constexpr std::array body{
        immediate(0x23, 0, 1, 0x400), immediate(9, 1, 2, 1),        immediate(0x2b, 0, 2, 0x400),
        immediate(0x23, 0, 3, 0xfff), immediate(0x29, 0, 3, 0xfff), immediate(0x24, 0, 4, 0x408),
        immediate(9, 4, 5, -1),       immediate(0x28, 0, 5, 0x409),
    };
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *machines;
    ordinary.rsp.set_native_execution(false);
    for (auto* machine : {&native, &ordinary}) {
        test::initialize_memory(*machine);
        install(*machine, body);
    }
    compare(native, ordinary, 8192);
    require_native(native);
    for (auto* machine : {&native, &ordinary}) {
        for (u32 offset = 0; offset < 64; offset += 4)
            machine->bus.memory.write(0x2000U + offset, 4, 0x76543210U + offset);
        machine->rsp.write_register(0, 0x400);
        machine->rsp.write_register(4, 0x2000);
        machine->rsp.write_register(8, 63);
    }
    for (u64 cycles : {1U, 8U, 1U, 2U, 1U, 17U, 2048U})
        compare(native, ordinary, cycles);
}

TEST(rsp_native_blocks_can_switch_to_ordinary_execution_and_reset) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *machines;
    ordinary.rsp.set_native_execution(false);
    for (auto* machine : {&native, &ordinary})
        install(*machine, scalar_body);
    compare(native, ordinary, 8192);
    require_native(native);
    const u64 before = native.rsp.native_block_instructions();
    native.rsp.set_native_execution(false);
    compare(native, ordinary, 2048);
    CHECK_EQ(native.rsp.native_block_instructions(), before);
    native.rsp.set_native_execution(true);
    compare(native, ordinary, 2048);
    native.rsp.reset();
    CHECK_EQ(native.rsp.native_block_instructions(), 0U);
}

TEST(rsp_native_blocks_stop_using_code_after_a_direct_imem_alias_is_exposed) {
    auto machines = std::make_unique<std::array<System, 2>>();
    auto& [native, ordinary] = *machines;
    ordinary.rsp.set_native_execution(false);
    for (auto* machine : {&native, &ordinary})
        install(*machine, scalar_body);
    compare(native, ordinary, 8192);
    require_native(native);
    const u64 before = native.rsp.native_block_instructions();
    for (auto* machine : {&native, &ordinary}) {
        auto* bytes = machine->rsp.memory.data();
        write_be32(bytes + 0x10f8, immediate(9, 0, 3, 0x456));
        machine->rsp.write_pc(0xf8);
    }
    compare(native, ordinary, 4096);
    CHECK_EQ(native.rsp.native_block_instructions(), before);
    CHECK(!native.rsp.memory.imem_trusted());
}

TEST(rsp_native_blocks_restore_speculative_stores_before_cpu_and_callback_observation) {
    constexpr std::array body{
        immediate(0x23, 0, 1, 0x400),
        immediate(9, 1, 1, 1),
        immediate(0x2b, 0, 1, 0x400),
        immediate(0x28, 0, 1, 0xfff),
        immediate(0x29, 0, 1, 0xfff),
        0xc8012040U,
        0x4a010890U,
        0xe8022041U,
    };
    using Observation = std::array<u64, 5>;
    for (unsigned phase = 0; phase < 3; ++phase) {
        auto machines = std::make_unique<std::array<System, 2>>();
        auto& [native, ordinary] = *machines;
        ordinary.rsp.set_native_execution(false);
        std::array<std::vector<Observation>, 2> observations;
        for (unsigned index = 0; index < 2; ++index) {
            auto& machine = (*machines)[index];
            test::initialize_memory(machine);
            machine.cpu.write_cop0(12, 0x34000401U);
            const std::array cpu_program{
                0U,
                immediate(9, 8, 8, 1),
                immediate(0x23, 16, 9, 0),
                immediate(0x0e, 9, 10, 0x55),
                immediate(5, 8, 0, -4),
                0U,
            };
            for (unsigned offset = 0; offset < cpu_program.size(); ++offset)
                machine.bus.write(0x1000U + offset * 4U, 4, cpu_program[offset]);
            machine.cpu.gpr[16] = 0xffffffffa4000400ULL;
            machine.cpu.set_pc(0xffffffff80001000ULL);
            machine.cpu.step();
            machine.advance(phase);
            install(machine, body);
            machine.bus.write(0x04500010, 4, 99);
            machine.bus.set_audio_sample_output(
                [machine = &machine, output = &observations[index]](const AudioSample&) {
                    output->push_back({machine->cpu.cycles, machine->rsp.pc, machine->bus.read(0x04000400, 4),
                                       machine->bus.read(0x04000fff, 1), machine->bus.output_clock()});
                });
        }
        for (unsigned count : {8192U, 1U, 2U, 7U, 31U, 97U, 2048U}) {
            for (unsigned step = 0; step < count; ++step)
                ordinary.cpu.step();
            CHECK_EQ(native.cpu.run_slice(count, 1'000'000), count);
            native.settle();
            ordinary.settle();
            CHECK_EQ(native.cpu.cycles, ordinary.cpu.cycles);
            CHECK_EQ(native.cpu.pc, ordinary.cpu.pc);
            CHECK_EQ(native.cpu.next_pc, ordinary.cpu.next_pc);
            CHECK(native.cpu.gpr == ordinary.cpu.gpr);
            CHECK(native.cpu.cp0 == ordinary.cpu.cp0);
            CHECK(native.rsp.memory == ordinary.rsp.memory);
            CHECK_EQ(native.rsp.pc, ordinary.rsp.pc);
            CHECK(observations[0] == observations[1]);
        }
        require_native(native);
        CHECK(!observations[0].empty());
        CHECK_EQ(ordinary.rsp.native_block_instructions(), 0U);
    }
}

TEST(rsp_native_code_is_independent_of_the_executing_register_storage) {
    using Op = RspPipeline::Operation;
    const std::array code{RspNativeInstruction{immediate(9, 1, 1, 7), Op::Addiu},
                          RspNativeInstruction{special(0x26, 1, 2, 3), Op::Xor}};
    const auto program = RspNativeCode::compile(code);
    CHECK_EQ(static_cast<bool>(program), RspNativeCode::available());
    if (!program)
        return;
    std::array<u32, 32> first{}, second{};
    first[1] = 0xfffffffc;
    first[2] = 0xf0f0;
    second[1] = 0x7fffffff;
    second[2] = 0x5555;
    RspNativeState first_state{nullptr, first.data(), nullptr};
    RspNativeState second_state{nullptr, second.data(), nullptr};
    program->execute(first_state);
    program->execute(second_state);
    CHECK_EQ(first[1], 3U);
    CHECK_EQ(first[3], 0xf0f3U);
    CHECK_EQ(second[1], 0x80000006U);
    CHECK_EQ(second[3], 0x80005553U);
}

TEST(rsp_native_code_rejects_control_flow_and_oversized_blocks) {
    using Op = RspPipeline::Operation;
    for (Op operation : {Op::J, Op::Jal, Op::Jr, Op::Jalr, Op::Beq, Op::Break, Op::Cop0}) {
        const std::array code{RspNativeInstruction{0, operation}};
        CHECK(!RspNativeCode::compile(code));
    }
    CHECK(!RspNativeCode::compile({}));
    const std::array<RspNativeInstruction, 17> oversized{};
    CHECK(!RspNativeCode::compile(oversized));
}
