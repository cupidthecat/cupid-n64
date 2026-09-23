#include "cupid/system.hpp"
#include "test.hpp"

#include <algorithm>
#include <array>
#include <vector>

using namespace cupid;

namespace {

constexpr unsigned dmem_size = 4096;
constexpr u32 vector_seed = 0x700;
constexpr u32 vector_output = 0x800;

constexpr u32 immediate(unsigned opcode, unsigned rt, unsigned rs, s16 value) {
    return (opcode << 26U) | (rs << 21U) | (rt << 16U) | static_cast<u16>(value);
}

constexpr u32 vector_memory(unsigned opcode, unsigned kind, unsigned vt, unsigned element, s8 offset,
                            unsigned base) {
    return (opcode << 26U) | (base << 21U) | (vt << 16U) | (kind << 11U) | ((element & 15U) << 7U) |
           (static_cast<u8>(offset) & 0x7fU);
}

constexpr u32 vector_load(unsigned kind, unsigned vt, unsigned element, s8 offset, unsigned base) {
    return vector_memory(0x32U, kind, vt, element, offset, base);
}

constexpr u32 vector_store(unsigned kind, unsigned vt, unsigned element, s8 offset, unsigned base) {
    return vector_memory(0x3aU, kind, vt, element, offset, base);
}

constexpr u32 break_instruction() {
    return 0x0000'000dU;
}

void run(Rsp& rsp, const std::vector<u32>& program) {
    for (unsigned index = 0; index < program.size(); ++index)
        write_be32(rsp.memory.data() + 0x1000U + index * 4U, program[index]);
    rsp.write_pc(0);
    rsp.write_register(0x10, 1U);
    rsp.tick(4096);
    CHECK_EQ(rsp.read_register(0x10) & 3U, 3U);
}

std::array<u8, 16> seed_vector(Rsp& rsp) {
    std::array<u8, 16> seed{};
    for (unsigned index = 0; index < seed.size(); ++index) {
        seed[index] = static_cast<u8>(0x80U + index * 7U);
        rsp.memory[vector_seed + index] = seed[index];
    }
    return seed;
}

void append_vector_seed(std::vector<u32>& program, unsigned vector) {
    program.push_back(immediate(0x09U, 3, 0, static_cast<s16>(vector_seed)));
    for (unsigned element = 0; element < 16; ++element)
        program.push_back(vector_load(0x00U, vector, element, static_cast<s8>(element), 3));
}

void append_vector_observation(std::vector<u32>& program, unsigned vector) {
    program.push_back(immediate(0x09U, 2, 0, static_cast<s16>(vector_output)));
    for (unsigned element = 0; element < 16; ++element)
        program.push_back(vector_store(0x00U, vector, element, static_cast<s8>(element), 2));
}

unsigned transfer_scale(unsigned kind) {
    if (kind == 0x01U)
        return 2;
    if (kind == 0x02U)
        return 4;
    if (kind == 0x03U)
        return 8;
    return 16;
}

struct AddressOperand {
    u32 base{};
    s8 offset{};
};

AddressOperand address_operand(u32 effective, unsigned scale, unsigned element) {
    const s8 offset = (element & 1U) != 0 ? -1 : 1;
    const s32 displacement = static_cast<s32>(offset) * static_cast<s32>(scale);
    return {static_cast<u32>(static_cast<s32>(effective) - displacement), offset};
}

u8 patterned_byte(u32 address) {
    return static_cast<u8>((address * 37U + 0x53U) ^ (address >> 4U));
}

void fill_pattern(Rsp& rsp) {
    for (u32 address = 0; address < dmem_size; ++address)
        rsp.memory[address] = patterned_byte(address);
}

std::array<u8, dmem_size> dmem_snapshot(const Rsp& rsp) {
    std::array<u8, dmem_size> result{};
    std::copy_n(rsp.memory.begin(), result.size(), result.begin());
    return result;
}

void apply_expected_load(std::array<u8, 16>& target, const std::array<u8, dmem_size>& dmem, unsigned kind,
                         unsigned element, u32 address) {
    if (kind >= 0x01U && kind <= 0x03U) {
        const unsigned count = std::min(1U << kind, 16U - element);
        for (unsigned index = 0; index < count; ++index)
            target[element + index] = dmem[(address + index) & 0x0fffU];
        return;
    }
    if (kind == 0x04U) {
        const unsigned count = std::min(16U - element, 16U - (address & 15U));
        for (unsigned index = 0; index < count; ++index)
            target[element + index] = dmem[(address + index) & 0x0fffU];
        return;
    }

    const unsigned offset = address & 15U;
    const unsigned count = offset > element ? offset - element : 0U;
    const u32 source = (address & 0x0fffU) & ~15U;
    for (unsigned index = 0; index < count; ++index)
        target[16U - count + index] = dmem[source + index];
}

void apply_expected_store(std::array<u8, dmem_size>& dmem, const std::array<u8, 16>& source, unsigned kind,
                          unsigned element, u32 address) {
    unsigned count = 0;
    u32 destination = address;
    unsigned source_start = element;
    if (kind >= 0x01U && kind <= 0x03U) {
        count = 1U << kind;
    } else if (kind == 0x04U) {
        count = 16U - (address & 15U);
    } else {
        count = address & 15U;
        destination &= ~15U;
        source_start = (element + 16U - count) & 15U;
    }
    for (unsigned index = 0; index < count; ++index)
        dmem[(destination + index) & 0x0fffU] = source[(source_start + index) & 15U];
}

u32 bytes_as_word(const std::array<u8, dmem_size>& dmem, u32 address, unsigned width) {
    u32 value = 0;
    for (unsigned index = 0; index < width; ++index)
        value = (value << 8U) | dmem[(address + index) & 0x0fffU];
    return value;
}

} // namespace

TEST(rsp_packed_scalar_dmem_reads_and_writes_preserve_unaligned_and_wrapped_bytes) {
    System system;
    constexpr std::array<u32, 4> half_addresses{0x120U, 0x123U, 0x0ffeU, 0x0fffU};
    constexpr std::array<u32, 5> word_addresses{0x140U, 0x143U, 0x0ffcU, 0x0ffdU, 0x0fffU};

    const auto check_width = [&](unsigned width, const auto& addresses) {
        for (const u32 address : addresses) {
            system.rsp.reset();
            std::fill_n(system.rsp.memory.begin(), dmem_size, u8{0xcc});
            const u32 value = width == 2 ? 0x0000a17eU : 0x89abcdefU;
            std::vector<u32> program{
                immediate(0x09U, 1, 0, static_cast<s16>(address)),
                immediate(0x0fU, 2, 0, static_cast<s16>(value >> 16U)),
                immediate(0x0dU, 2, 2, static_cast<s16>(value)),
                immediate(width == 2 ? 0x29U : 0x2bU, 2, 1, 0),
                break_instruction(),
            };
            auto expected = dmem_snapshot(system.rsp);
            for (unsigned byte = 0; byte < width; ++byte)
                expected[(address + byte) & 0x0fffU] = static_cast<u8>(value >> ((width - byte - 1U) * 8U));
            run(system.rsp, program);
            CHECK(std::equal(expected.begin(), expected.end(), system.rsp.memory.begin()));

            system.rsp.reset();
            fill_pattern(system.rsp);
            const auto input = dmem_snapshot(system.rsp);
            constexpr u32 output = 0x900U;
            program = {
                immediate(0x09U, 1, 0, static_cast<s16>(address)),
                immediate(width == 2 ? 0x25U : 0x23U, 2, 1, 0),
                immediate(0x09U, 3, 0, static_cast<s16>(output)),
                immediate(0x2bU, 2, 3, 0),
                break_instruction(),
            };
            run(system.rsp, program);
            CHECK_EQ(read_be32(system.rsp.memory.data() + output), bytes_as_word(input, address, width));
        }
    };
    check_width(2, half_addresses);
    check_width(4, word_addresses);
}

TEST(rsp_packed_vector_loads_preserve_elements_truncation_and_dmem_wrap) {
    System system;
    constexpr std::array<u32, 8> addresses{0U, 0x120U, 0x121U, 0x123U, 0x12fU, 0x0ff0U, 0x0ff1U, 0x0fffU};

    for (unsigned kind = 0x01U; kind <= 0x05U; ++kind) {
        const unsigned scale = transfer_scale(kind);
        for (unsigned element = 0; element < 16; ++element) {
            for (const u32 address : addresses) {
                system.rsp.reset();
                fill_pattern(system.rsp);
                const auto seed = seed_vector(system.rsp);
                const auto input = dmem_snapshot(system.rsp);
                auto expected = seed;
                apply_expected_load(expected, input, kind, element, address);

                const auto operand = address_operand(address, scale, element);
                std::vector<u32> program;
                append_vector_seed(program, 4);
                program.push_back(immediate(0x09U, 1, 0, static_cast<s16>(operand.base)));
                program.push_back(vector_load(kind, 4, element, operand.offset, 1));
                append_vector_observation(program, 4);
                program.push_back(break_instruction());
                run(system.rsp, program);

                for (unsigned byte = 0; byte < expected.size(); ++byte)
                    CHECK_EQ(system.rsp.memory[vector_output + byte], expected[byte]);
            }
        }
    }
}

TEST(rsp_packed_vector_stores_preserve_source_and_dmem_wrap_with_sentinels) {
    System system;
    constexpr std::array<u32, 8> addresses{0U, 0x120U, 0x121U, 0x123U, 0x12fU, 0x0ff0U, 0x0ff1U, 0x0fffU};

    for (unsigned kind = 0x01U; kind <= 0x05U; ++kind) {
        const unsigned scale = transfer_scale(kind);
        for (unsigned element = 0; element < 16; ++element) {
            for (const u32 address : addresses) {
                system.rsp.reset();
                std::fill_n(system.rsp.memory.begin(), dmem_size, u8{0xcc});
                const auto seed = seed_vector(system.rsp);
                auto expected = dmem_snapshot(system.rsp);
                apply_expected_store(expected, seed, kind, element, address);

                const auto operand = address_operand(address, scale, element);
                std::vector<u32> program;
                append_vector_seed(program, 4);
                program.push_back(immediate(0x09U, 1, 0, static_cast<s16>(operand.base)));
                program.push_back(vector_store(kind, 4, element, operand.offset, 1));
                program.push_back(break_instruction());
                run(system.rsp, program);

                CHECK(std::equal(expected.begin(), expected.end(), system.rsp.memory.begin()));
            }
        }
    }
}
