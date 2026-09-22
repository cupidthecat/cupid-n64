#include "triangle_commands.hpp"

#include <algorithm>
#include <array>
#include <future>
#include <thread>

using namespace cupid;
using namespace test::rdp;

namespace {
constexpr u32 color_base = 0x100000U;
constexpr u32 depth_base = 0x200000U;
constexpr unsigned width = 128;
constexpr unsigned height = 64;

void prepare(TriangleCommands& commands, bool parallel, unsigned size, u32 color = color_base,
             u32 depth = depth_base, unsigned stride = width) {
    commands.system->bus.rdp.set_parallel_rasterization(parallel);
    for (u32 offset = 0; offset < width * height * 4; offset += 2) {
        commands.system->bus.memory.write(color_base + offset, 2, (offset * 1597U + 123U) & 65535U);
        commands.system->bus.memory.set_hidden_pair(color_base + offset,
                                                    static_cast<u8>((offset >> 2U) & 3U));
        commands.system->bus.memory.write(depth_base + offset, 2, 0xfffcU);
        commands.system->bus.memory.set_hidden_pair(depth_base + offset, 3);
    }
    commands.append(0x3f, (static_cast<u64>(size) << 51U) | (static_cast<u64>(stride - 1U) << 32U) | color);
    commands.append(0x3e, depth);
    commands.append(0x2d, (static_cast<u64>(width * 4U) << 12U) | (height * 4U));
    commands.append(0x2e, 0x40000020U);
    commands.system->bus.memory.advance_clock(937);
}

void compare(const TriangleCommands& serial, const TriangleCommands& parallel) {
    const auto& first = serial.system->bus;
    const auto& second = parallel.system->bus;
    CHECK_EQ(first.rdram, second.rdram);
    CHECK(std::equal(first.memory.hidden_memory().begin(), first.memory.hidden_memory().end(),
                     second.memory.hidden_memory().begin()));
    CHECK_EQ(first.memory.bank_status(), second.memory.bank_status());
    CHECK_EQ(first.memory.errors(), second.memory.errors());
    CHECK_EQ(first.memory.clock(), second.memory.clock());
    for (u32 bank = 0; bank < 8; ++bank) {
        const u32 base = bank << 20U;
        CHECK_EQ(first.memory.bank_access_clock(base), second.memory.bank_access_clock(base));
        for (u32 row = 0; row < 512; ++row)
            CHECK_EQ(first.memory.row_open(base + (row << 11U)), second.memory.row_open(base + (row << 11U)));
    }
    for (u32 offset = 0; offset < 32; offset += 4)
        CHECK_EQ(first.rdp.read_register(offset), second.rdp.read_register(offset));
}

void run_outside_framebuffers(TriangleCommands& commands) {
    // The wrapping draw reaches low RDRAM, including the fixture's usual command
    // address. Keep SyncFull outside that range so command completion is observable.
    constexpr u32 command_base = 0x10000;
    commands.append(0x29, 0);
    auto& bus = commands.system->bus;
    for (u32 address = 0x1000; address < commands.end; address += 8)
        bus.memory.write(command_base + address - 0x1000, 8, bus.memory.read(address, 8));
    const u32 end = command_base + commands.end - 0x1000;
    bus.rdp.write_register(0, command_base);
    bus.rdp.write_register(4, end);
    CHECK_EQ(bus.rdp.current(), end);
    CHECK_EQ(bus.rdp.read_register(12) & 0x62U, 0U);
}

void draw(TriangleCommands& commands, unsigned variant) {
    const bool two_cycles = (variant & 1U) != 0;
    const u64 depth = (variant & 2U) != 0 ? 0x30U : 0U;
    const u64 noise = (variant & 4U) != 0 ? (2ULL << 38U) | (2ULL << 36U) | 3U : 0U;
    commands.append(0x2f, (two_cycles ? 1ULL << 52U : 0U) | depth | noise | 0x4cU | (1ULL << 14U) |
                              (1ULL << 22U) | (1ULL << 18U));
    commands.append(
        0x3c, combine_word(
                  {.d = 1, .ad = 1},
                  {.a = two_cycles ? 0U : 1U, .b = 8, .c = 3, .d = 5, .aa = 7, .ab = 7, .ac = 7, .ad = 3}));
    commands.append(0x3b, 0x11335577U);
    commands.rectangle(1, 2, width * 4U - 1U, height * 4U - 1U);
    commands.append(0x3a, 0x80c040b0U);
    commands.textured(3, 1, width * 4U - 2U, height * 4U - 3U, 17, 61, 777, -391, true);
    if ((variant & 4U) != 0)
        commands.append(0x2d, (3ULL << 24U) | (static_cast<u64>(width * 4U) << 12U) | (height * 4U));
    commands.triangle(0x0f, {.top = 1,
                             .middle = 127,
                             .bottom = 255,
                             .major = 0,
                             .upper = 0x007f0000,
                             .lower = 0x007f0000,
                             .major_step = 0,
                             .upper_step = -1024,
                             .lower_step = -1024,
                             .left_major = true});
    commands.triangle(0x0f, {.top = 3,
                             .middle = 125,
                             .bottom = 253,
                             .major = 0x007e0000,
                             .upper = 0x00010000,
                             .lower = 0x00010000,
                             .major_step = 0,
                             .upper_step = 1024,
                             .lower_step = 1024,
                             .left_major = false});
    commands.run();
}
} // namespace

TEST(rdp_parallel_rows_match_serial_pixels_hidden_bits_depth_and_bank_order) {
    for (const unsigned size : {2U, 3U}) {
        for (unsigned variant = 0; variant < 8; ++variant) {
            TriangleCommands serial;
            TriangleCommands parallel;
            prepare(serial, false, size);
            prepare(parallel, true, size);
            draw(serial, variant);
            draw(parallel, variant);
            compare(serial, parallel);
            CHECK_EQ(serial.system->bus.rdp.parallel_draws(), 0U);
            if (std::thread::hardware_concurrency() > 1)
                CHECK(parallel.system->bus.rdp.parallel_draws() != 0);
        }
    }
}

TEST(rdp_parallel_rows_keep_aliases_wrap_small_formats_and_stride_overruns_serial) {
    for (unsigned variant = 0; variant < 7; ++variant) {
        TriangleCommands serial;
        TriangleCommands parallel;
        const unsigned size = variant < 2 ? variant : 2U;
        const u32 color = variant == 2 ? 0x7ff000U : color_base;
        const u32 depth = variant == 3 ? color_base + 2U : variant == 4 ? color_base - 256U : depth_base;
        const unsigned stride = variant == 5 ? width - 1U : width;
        prepare(serial, false, size, color, depth, stride);
        prepare(parallel, true, size, color, depth, stride);
        if (variant == 6) {
            for (auto* commands : {&serial, &parallel})
                commands->system->bus.memory.write_register(0x03f0080cU, 0);
        }
        for (auto* commands : {&serial, &parallel}) {
            commands->modes(0x34U);
            commands->rectangle(0, 0, width * 4U, height * 4U);
            run_outside_framebuffers(*commands);
        }
        compare(serial, parallel);
        CHECK_EQ(parallel.system->bus.rdp.parallel_draws(), 0U);
    }
}

TEST(rdp_parallel_rows_keep_concurrent_machine_state_independent) {
    std::array<std::future<void>, 3> runs;
    for (unsigned index = 0; index < runs.size(); ++index) {
        runs[index] = std::async(std::launch::async, [index] {
            TriangleCommands serial;
            TriangleCommands parallel;
            prepare(serial, false, 2U + (index & 1U));
            prepare(parallel, true, 2U + (index & 1U));
            draw(serial, index + 4U);
            draw(parallel, index + 4U);
            compare(serial, parallel);
        });
    }
    for (auto& run : runs)
        run.get();
}
