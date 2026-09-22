#include "cupid/system.hpp"
#include "test.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <vector>

using namespace cupid;

namespace {

constexpr u32 seed_address = 0x180U;
constexpr u32 output_address = 0x280U;

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

constexpr u32 vector_op(unsigned function, unsigned vd, unsigned vs, unsigned vt, unsigned element) {
    return (0x12U << 26U) | ((0x10U | (element & 15U)) << 21U) | (vt << 16U) | (vs << 11U) | (vd << 6U) |
           (function & 63U);
}

constexpr u32 cop2_transfer(unsigned rs, unsigned rt, unsigned rd, unsigned element) {
    return (0x12U << 26U) | (rs << 21U) | (rt << 16U) | (rd << 11U) | ((element & 15U) << 7U);
}

void run(Rsp& rsp, const std::vector<u32>& program) {
    for (unsigned index = 0; index < program.size(); ++index)
        write_be32(rsp.memory.data() + 0x1000U + index * 4U, program[index]);
    rsp.write_pc(0);
    rsp.write_register(0x10, 1U);
    rsp.tick(4096);
    CHECK_EQ(rsp.read_register(0x10) & 3U, 3U);
}

std::array<u8, 16> seed_bytes(Rsp& rsp, u32 address = seed_address) {
    std::array<u8, 16> bytes{};
    for (unsigned index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<u8>(0x31U + index * 11U);
        rsp.memory[(address + index) & 0x0fffU] = bytes[index];
    }
    return bytes;
}

void append_byte_seed(std::vector<u32>& program, unsigned vector, unsigned base_register = 1) {
    program.push_back(immediate(0x09U, base_register, 0, static_cast<s16>(seed_address)));
    for (unsigned element = 0; element < 16; ++element)
        program.push_back(vector_load(0x00U, vector, element, static_cast<s8>(element), base_register));
}

void append_byte_observation(std::vector<u32>& program, unsigned vector, unsigned base_register = 3,
                             u32 address = output_address) {
    program.push_back(immediate(0x09U, base_register, 0, static_cast<s16>(address)));
    for (unsigned element = 0; element < 16; ++element)
        program.push_back(vector_store(0x00U, vector, element, static_cast<s8>(element), base_register));
}

std::array<u8, 16> read_bytes(const Rsp& rsp, u32 address = output_address) {
    std::array<u8, 16> bytes{};
    for (unsigned index = 0; index < bytes.size(); ++index)
        bytes[index] = rsp.memory[(address + index) & 0x0fffU];
    return bytes;
}

std::array<u16, 8> words_from_bytes(const std::array<u8, 16>& bytes) {
    std::array<u16, 8> words{};
    for (unsigned lane = 0; lane < words.size(); ++lane)
        words[lane] = static_cast<u16>((static_cast<u16>(bytes[lane * 2U]) << 8U) | bytes[lane * 2U + 1U]);
    return words;
}

unsigned selected_lane(unsigned element, unsigned lane) {
    lane &= 7U;
    switch (element & 15U) {
    case 0:
    case 1:
        return lane;
    case 2:
        return lane & ~1U;
    case 3:
        return lane | 1U;
    case 4:
    case 5:
    case 6:
    case 7:
        return lane < 4U ? element - 4U : element;
    default:
        return element - 8U;
    }
}

void fill_pattern(Rsp& rsp) {
    for (u32 address = 0; address < 0x1000U; ++address)
        rsp.memory[address] = static_cast<u8>((address * 29U + 0x47U) ^ (address >> 3U));
}

std::array<u8, 16> expected_special_load(const std::array<u8, 16>& seed, const Rsp& rsp, unsigned kind,
                                         unsigned element, u32 address) {
    auto expected = seed;
    const auto memory_byte = [&](u32 location) { return rsp.memory[location & 0x0fffU]; };
    const auto set_word = [&](unsigned lane, u16 value) {
        expected[lane * 2U] = static_cast<u8>(value >> 8U);
        expected[lane * 2U + 1U] = static_cast<u8>(value);
    };

    if (kind == 0x06U || kind == 0x07U) {
        const u32 index = (address & 7U) - element;
        address &= ~7U;
        const unsigned shift = kind == 0x06U ? 8U : 7U;
        for (unsigned lane = 0; lane < 8; ++lane)
            set_word(lane, static_cast<u16>(static_cast<u16>(memory_byte(address + ((index + lane) & 15U)))
                                            << shift));
    } else if (kind == 0x08U) {
        const u32 index = (address & 7U) - element;
        address &= ~7U;
        for (unsigned lane = 0; lane < 8; ++lane)
            set_word(lane, static_cast<u16>(
                               static_cast<u16>(memory_byte(address + ((index + lane * 2U) & 15U))) << 7U));
    } else if (kind == 0x09U) {
        const u32 index = (address & 7U) - element;
        address &= ~7U;
        std::array<u8, 16> temporary{};
        for (unsigned lane = 0; lane < 4; ++lane) {
            const u16 low =
                static_cast<u16>(static_cast<u16>(memory_byte(address + ((index + lane * 4U) & 15U))) << 7U);
            const u16 high = static_cast<u16>(
                static_cast<u16>(memory_byte(address + ((index + lane * 4U + 8U) & 15U))) << 7U);
            temporary[lane * 2U] = static_cast<u8>(low >> 8U);
            temporary[lane * 2U + 1U] = static_cast<u8>(low);
            temporary[(lane + 4U) * 2U] = static_cast<u8>(high >> 8U);
            temporary[(lane + 4U) * 2U + 1U] = static_cast<u8>(high);
        }
        for (unsigned byte = element; byte < std::min(element + 8U, 16U); ++byte)
            expected[byte] = temporary[byte];
    }
    return expected;
}

void apply_special_store(std::array<u8, 4096>& expected, const std::array<u8, 16>& source, unsigned kind,
                         unsigned element, u32 address) {
    const auto word = [&](unsigned lane) {
        return static_cast<u16>((static_cast<u16>(source[(lane & 7U) * 2U]) << 8U) |
                                source[(lane & 7U) * 2U + 1U]);
    };
    const auto put = [&](u32 location, u8 value) { expected[location & 0x0fffU] = value; };

    if (kind == 0x06U || kind == 0x07U) {
        for (unsigned byte = element; byte < element + 8U; ++byte) {
            const unsigned index = byte & 15U;
            u8 value = 0;
            if (kind == 0x06U)
                value = index < 8U ? source[(index & 7U) * 2U] : static_cast<u8>(word(index) >> 7U);
            else
                value = index < 8U ? static_cast<u8>(word(index) >> 7U) : source[(index & 7U) * 2U];
            put(address++, value);
        }
        return;
    }

    if (kind == 0x08U) {
        const u32 index = address & 7U;
        address &= ~7U;
        for (unsigned lane = 0; lane < 8; ++lane) {
            const unsigned byte = element + lane * 2U;
            const u8 value = static_cast<u8>((static_cast<u16>(source[byte & 15U]) << 1U) |
                                             (source[(byte + 1U) & 15U] >> 7U));
            put(address + ((index + lane * 2U) & 15U), value);
        }
        return;
    }

    if (kind == 0x09U) {
        const u32 base = address & 7U;
        address &= ~7U;
        std::array<unsigned, 4> lanes{};
        bool valid = true;
        switch (element) {
        case 0:
        case 15:
            lanes = {0, 1, 2, 3};
            break;
        case 1:
            lanes = {6, 7, 4, 5};
            break;
        case 4:
            lanes = {1, 2, 3, 0};
            break;
        case 5:
            lanes = {7, 4, 5, 6};
            break;
        case 8:
            lanes = {4, 5, 6, 7};
            break;
        case 11:
            lanes = {3, 0, 1, 2};
            break;
        case 12:
            lanes = {5, 6, 7, 4};
            break;
        default:
            valid = false;
            break;
        }
        for (unsigned index = 0; index < 4; ++index)
            put(address + ((base + index * 4U) & 15U), valid ? static_cast<u8>(word(lanes[index]) >> 7U) : 0);
        return;
    }

    if (kind == 0x0aU) {
        u32 base = address & 7U;
        address &= ~7U;
        for (unsigned byte = element; byte < element + 16U; ++byte)
            put(address + (base++ & 15U), source[byte & 15U]);
    }
}

} // namespace

TEST(rsp_native_vector_mfc2_mtc2_keep_architectural_byte_order_at_every_element) {
    for (unsigned element = 0; element < 16; ++element) {
        System system;
        const auto seed = seed_bytes(system.rsp);
        std::vector<u32> program;
        append_byte_seed(program, 1);
        program.push_back(cop2_transfer(0x00U, 2, 1, element));
        program.push_back(immediate(0x09U, 3, 0, static_cast<s16>(output_address)));
        program.push_back(immediate(0x2bU, 2, 3, 0));
        program.push_back(0x0000'000dU);
        run(system.rsp, program);

        const u16 raw =
            static_cast<u16>((static_cast<u16>(seed[element]) << 8U) | seed[(element + 1U) & 15U]);
        const u32 expected = static_cast<u32>(static_cast<s32>(std::bit_cast<s16>(raw)));
        CHECK_EQ(read_be32(system.rsp.memory.data() + output_address), expected);

        system.rsp.reset();
        const auto mtc_seed = seed_bytes(system.rsp);
        program.clear();
        append_byte_seed(program, 1);
        program.push_back(immediate(0x0dU, 2, 0, std::bit_cast<s16>(u16{0xabcdU})));
        program.push_back(cop2_transfer(0x04U, 2, 1, element));
        append_byte_observation(program, 1);
        program.push_back(0x0000'000dU);
        run(system.rsp, program);

        auto expected_bytes = mtc_seed;
        expected_bytes[element] = 0xabU;
        if (element != 15U)
            expected_bytes[element + 1U] = 0xcdU;
        CHECK(read_bytes(system.rsp) == expected_bytes);
    }
}

TEST(rsp_native_vector_special_loads_keep_lane_values_and_partial_bytes) {
    constexpr std::array<unsigned, 4> elements{0U, 3U, 8U, 15U};
    constexpr std::array<u32, 3> addresses{0x121U, 0x127U, 0x0fffU};
    for (const unsigned kind : {0x06U, 0x07U, 0x08U, 0x09U}) {
        const unsigned scale = kind < 0x08U ? 8U : 16U;
        for (const unsigned element : elements) {
            for (const u32 address : addresses) {
                System system;
                fill_pattern(system.rsp);
                const auto seed = seed_bytes(system.rsp);
                const auto expected = expected_special_load(seed, system.rsp, kind, element, address);
                const s8 offset = 1;
                const u32 base = address - scale;
                std::vector<u32> program;
                append_byte_seed(program, 4);
                program.push_back(immediate(0x09U, 2, 0, static_cast<s16>(base)));
                program.push_back(vector_load(kind, 4, element, offset, 2));
                append_byte_observation(program, 4);
                program.push_back(0x0000'000dU);
                run(system.rsp, program);
                CHECK(read_bytes(system.rsp) == expected);
            }
        }
    }
}

TEST(rsp_native_vector_special_stores_keep_architectural_byte_formulas_and_wrap) {
    constexpr std::array<unsigned, 7> elements{0U, 1U, 3U, 4U, 8U, 12U, 15U};
    constexpr std::array<u32, 3> addresses{0x121U, 0x127U, 0x0fffU};
    for (const unsigned kind : {0x06U, 0x07U, 0x08U, 0x09U, 0x0aU}) {
        const unsigned scale = kind < 0x08U ? 8U : 16U;
        for (const unsigned element : elements) {
            for (const u32 address : addresses) {
                System system;
                std::fill_n(system.rsp.memory.begin(), 0x1000U, u8{0xcc});
                const auto seed = seed_bytes(system.rsp);
                std::array<u8, 4096> expected{};
                std::copy_n(system.rsp.memory.begin(), expected.size(), expected.begin());
                apply_special_store(expected, seed, kind, element, address);

                const u32 base = address - scale;
                std::vector<u32> program;
                append_byte_seed(program, 4);
                program.push_back(immediate(0x09U, 2, 0, static_cast<s16>(base)));
                program.push_back(vector_store(kind, 4, element, 1, 2));
                program.push_back(0x0000'000dU);
                run(system.rsp, program);
                CHECK(std::equal(expected.begin(), expected.end(), system.rsp.memory.begin()));
            }
        }
    }
}

TEST(rsp_native_vector_ltv_stv_keep_register_group_and_byte_rotation) {
    constexpr u32 group_seed = 0x400U;
    constexpr u32 group_output = 0x600U;
    constexpr unsigned vt = 10U;
    constexpr unsigned register_base = vt & ~7U;
    constexpr std::array<unsigned, 4> elements{0U, 3U, 8U, 15U};
    constexpr std::array<u32, 3> addresses{0x521U, 0x52fU, 0x0fffU};

    const auto initialize_group = [=](Rsp& rsp, std::array<std::array<u8, 16>, 8>& registers) {
        for (unsigned reg = 0; reg < registers.size(); ++reg) {
            for (unsigned byte = 0; byte < registers[reg].size(); ++byte) {
                const u8 value = static_cast<u8>(0x20U + reg * 0x17U + byte * 3U);
                registers[reg][byte] = value;
                rsp.memory[group_seed + reg * 16U + byte] = value;
            }
        }
    };
    const auto append_group_seed = [=](std::vector<u32>& program) {
        program.push_back(immediate(0x09U, 1, 0, static_cast<s16>(group_seed)));
        for (unsigned reg = 0; reg < 8; ++reg)
            program.push_back(vector_load(0x04U, register_base + reg, 0, static_cast<s8>(reg), 1));
    };
    const auto append_group_observation = [=](std::vector<u32>& program) {
        program.push_back(immediate(0x09U, 3, 0, static_cast<s16>(group_output)));
        for (unsigned reg = 0; reg < 8; ++reg)
            program.push_back(vector_store(0x04U, register_base + reg, 0, static_cast<s8>(reg), 3));
    };

    for (const unsigned element : elements) {
        for (const u32 effective : addresses) {
            System system;
            fill_pattern(system.rsp);
            std::array<std::array<u8, 16>, 8> expected_registers{};
            initialize_group(system.rsp, expected_registers);

            const u32 begin = effective & ~7U;
            u32 source = begin + ((element + (effective & 8U)) & 15U);
            unsigned register_offset = element >> 1U;
            for (unsigned lane = 0; lane < 8; ++lane) {
                auto& target = expected_registers[register_offset];
                target[lane * 2U] = system.rsp.memory[source & 0x0fffU];
                source = source + 1U == begin + 16U ? begin : source + 1U;
                target[lane * 2U + 1U] = system.rsp.memory[source & 0x0fffU];
                source = source + 1U == begin + 16U ? begin : source + 1U;
                register_offset = (register_offset + 1U) & 7U;
            }

            std::vector<u32> program;
            append_group_seed(program);
            program.push_back(immediate(0x09U, 2, 0, static_cast<s16>(effective - 16U)));
            program.push_back(vector_load(0x0bU, vt, element, 1, 2));
            append_group_observation(program);
            program.push_back(0x0000'000dU);
            run(system.rsp, program);
            for (unsigned reg = 0; reg < 8; ++reg) {
                for (unsigned byte = 0; byte < 16; ++byte)
                    CHECK_EQ(system.rsp.memory[group_output + reg * 16U + byte],
                             expected_registers[reg][byte]);
            }

            system.rsp.reset();
            std::array<std::array<u8, 16>, 8> source_registers{};
            std::fill_n(system.rsp.memory.begin(), 0x1000U, u8{0xcc});
            initialize_group(system.rsp, source_registers);
            std::array<u8, 4096> expected_memory{};
            std::copy_n(system.rsp.memory.begin(), expected_memory.size(), expected_memory.begin());
            const unsigned source_byte_start = 16U - (element & ~1U);
            u32 memory_offset = (effective & 7U) - (element & ~1U);
            const u32 destination = effective & ~7U;
            unsigned source_byte = source_byte_start;
            for (unsigned reg = 0; reg < 8; ++reg) {
                expected_memory[(destination + (memory_offset++ & 15U)) & 0x0fffU] =
                    source_registers[reg][source_byte++ & 15U];
                expected_memory[(destination + (memory_offset++ & 15U)) & 0x0fffU] =
                    source_registers[reg][source_byte++ & 15U];
            }

            program.clear();
            append_group_seed(program);
            program.push_back(immediate(0x09U, 2, 0, static_cast<s16>(effective - 16U)));
            program.push_back(vector_store(0x0bU, vt, element, 1, 2));
            program.push_back(0x0000'000dU);
            run(system.rsp, program);
            CHECK(std::equal(expected_memory.begin(), expected_memory.end(), system.rsp.memory.begin()));
        }
    }
}

TEST(rsp_native_vector_sse2_element_selection_and_aliases_keep_lane_order) {
    constexpr std::array<u16, 8> left{0x0011, 0x1022, 0x2033, 0x3044, 0x4055, 0x5066, 0x6077, 0x7088};
    constexpr std::array<u16, 8> right{0x8101, 0x7202, 0x6304, 0x5408, 0x4510, 0x3620, 0x2740, 0x1880};
    for (unsigned element = 0; element < 16; ++element) {
        for (unsigned alias = 0; alias < 4; ++alias) {
            System system;
            for (unsigned lane = 0; lane < 8; ++lane) {
                write_be16(system.rsp.memory.data() + seed_address + lane * 2U, left[lane]);
                write_be16(system.rsp.memory.data() + seed_address + 0x10U + lane * 2U, right[lane]);
            }
            const unsigned vs = 1U;
            const unsigned vt = alias == 3U ? 1U : 2U;
            const unsigned vd = alias == 0U ? 3U : alias == 1U ? 1U : alias == 2U ? 2U : 1U;
            std::vector<u32> program{
                immediate(0x09U, 1, 0, static_cast<s16>(seed_address)),
                vector_load(0x04U, 1, 0, 0, 1),
                vector_load(0x04U, 2, 0, 1, 1),
                vector_op(0x2aU, vd, vs, vt, element), // VOR.
                immediate(0x09U, 3, 0, static_cast<s16>(output_address)),
                vector_store(0x04U, vd, 0, 0, 3),
                0x0000'000dU,
            };
            run(system.rsp, program);

            std::array<u16, 8> expected{};
            const auto& right_source = alias == 3U ? left : right;
            for (unsigned lane = 0; lane < 8; ++lane)
                expected[lane] = static_cast<u16>(left[lane] | right_source[selected_lane(element, lane)]);
            const auto observed = words_from_bytes(read_bytes(system.rsp));
            CHECK(observed == expected);
        }
    }
}

TEST(rsp_native_vector_scalar_fallback_vmov_keeps_lane_order_and_aliases) {
    constexpr std::array<u16, 8> destination_seed{0x0011, 0x1022, 0x2033, 0x3044,
                                                  0x4055, 0x5066, 0x6077, 0x7088};
    constexpr std::array<u16, 8> source{0x8101, 0x7202, 0x6304, 0x5408, 0x4510, 0x3620, 0x2740, 0x1880};
    for (unsigned element = 0; element < 16; ++element) {
        for (unsigned destination_element = 0; destination_element < 8; ++destination_element) {
            for (const bool alias : {false, true}) {
                System system;
                for (unsigned lane = 0; lane < 8; ++lane) {
                    write_be16(system.rsp.memory.data() + seed_address + lane * 2U, destination_seed[lane]);
                    write_be16(system.rsp.memory.data() + seed_address + 0x10U + lane * 2U, source[lane]);
                }

                const unsigned vd = 1U;
                const unsigned vt = alias ? vd : 2U;
                std::vector<u32> program{
                    immediate(0x09U, 1, 0, static_cast<s16>(seed_address)),
                    vector_load(0x04U, vd, 0, 0, 1),
                    vector_load(0x04U, 2, 0, 1, 1),
                    vector_op(0x33U, vd, destination_element, vt, element), // VMOV uses scalar fallback.
                    immediate(0x09U, 3, 0, static_cast<s16>(output_address)),
                    vector_store(0x04U, vd, 0, 0, 3),
                    0x0000'000dU,
                };
                run(system.rsp, program);

                auto expected = destination_seed;
                const auto& selected_source = alias ? destination_seed : source;
                expected[destination_element] = selected_source[selected_lane(element, destination_element)];
                CHECK(words_from_bytes(read_bytes(system.rsp)) == expected);
            }
        }
    }
}

TEST(rsp_native_vector_vsaw_reads_native_accumulator_slices_without_reordering) {
    constexpr std::array<u16, 8> left{0xffff, 0x0001, 0x8000, 0x7fff, 0xff00, 0x0100, 0xc000, 0x4000};
    constexpr std::array<u16, 8> right{0x0002, 0xffff, 0x0003, 0x8000, 0x0101, 0x0202, 0x7fff, 0x8001};
    constexpr std::array<u16, 8> high{0xffff, 0x0000, 0xffff, 0x0000, 0xffff, 0x0000, 0xffff, 0x0000};
    constexpr std::array<u16, 8> middle{0xffff, 0x0000, 0xfffe, 0x3fff, 0xfffe, 0x0002, 0xe000, 0x2000};
    constexpr std::array<u16, 8> low{0xfffe, 0xffff, 0x8000, 0x8000, 0xff00, 0x0200, 0x4000, 0x4000};
    System system;
    for (unsigned lane = 0; lane < 8; ++lane) {
        write_be16(system.rsp.memory.data() + seed_address + lane * 2U, left[lane]);
        write_be16(system.rsp.memory.data() + seed_address + 0x10U + lane * 2U, right[lane]);
    }
    std::vector<u32> program{
        immediate(0x09U, 1, 0, static_cast<s16>(seed_address)),
        vector_load(0x04U, 1, 0, 0, 1),
        vector_load(0x04U, 2, 0, 1, 1),
        vector_op(0x05U, 7, 1, 2, 0), // VMUDM seeds all accumulator slices.
        immediate(0x09U, 3, 0, static_cast<s16>(output_address)),
    };
    for (unsigned slice = 0; slice < 4; ++slice) {
        program.push_back(vector_op(0x1dU, 8U + slice, 0, 0, 8U + slice));
        program.push_back(vector_store(0x04U, 8U + slice, 0, static_cast<s8>(slice), 3));
    }
    program.push_back(0x0000'000dU);
    run(system.rsp, program);

    for (unsigned lane = 0; lane < 8; ++lane) {
        CHECK_EQ(read_be16(system.rsp.memory.data() + output_address + lane * 2U), high[lane]);
        CHECK_EQ(read_be16(system.rsp.memory.data() + output_address + 0x10U + lane * 2U), middle[lane]);
        CHECK_EQ(read_be16(system.rsp.memory.data() + output_address + 0x20U + lane * 2U), low[lane]);
        CHECK_EQ(read_be16(system.rsp.memory.data() + output_address + 0x30U + lane * 2U), 0U);
    }
}
