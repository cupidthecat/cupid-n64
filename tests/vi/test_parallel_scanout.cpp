#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <future>

using namespace cupid;

namespace {
void prepare(System& system, u32 seed) {
    test::initialize_memory(system);
    for (u32 address = 0; address < 0x80000U; ++address) {
        seed = seed * 1664525U + 1013904223U;
        system.bus.rdram[address] = static_cast<u8>(seed >> 24U);
        if ((address & 1U) == 0)
            system.bus.memory.set_hidden_pair(address, static_cast<u8>(seed >> 30U));
    }
}

void registers(System& system, u32 control, u32 x_scale, u32 y_scale, bool clipped) {
    const bool pal = system.video_standard() == VideoStandard::Pal;
    const u32 horizontal = pal ? 128U : 108U;
    const u32 vertical = pal ? 44U : 34U;
    auto& bus = system.bus;
    bus.write(0x04400000, 4, control);
    bus.write(0x04400004, 4, 0x37fb);
    bus.write(0x04400008, 4, 320);
    bus.write(0x04400024, 4, ((horizontal - (clipped ? 11U : 0U)) << 16U) | (horizontal + 640U));
    bus.write(0x04400028, 4, ((vertical - (clipped ? 6U : 0U)) << 16U) | (vertical + 266U));
    bus.write(0x04400030, 4, (511U << 16U) | x_scale);
    bus.write(0x04400034, 4, (767U << 16U) | y_scale);
}

void equal_fields(const VideoField& first, const VideoField& second) {
    CHECK_EQ(first.width, second.width);
    CHECK_EQ(first.height, second.height);
    CHECK_EQ(first.field, second.field);
    CHECK_EQ(first.interlaced, second.interlaced);
    CHECK_EQ(first.pixels, second.pixels);
}
} // namespace

TEST(vi_parallel_scanout_matches_sequential_filters_and_clipped_scaling) {
    constexpr std::array<u32, 4> x_scales{511U, 1024U, 1536U, 2048U};
    constexpr std::array<u32, 4> y_scales{512U, 768U, 1024U, 1536U};
    for (const auto standard : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        System system(standard);
        prepare(system, 0x12463a78U);
        for (const u32 format : {2U, 3U}) {
            for (u32 controls = 0; controls < 16; ++controls) {
                const u32 mode = controls & 3U;
                const u32 filter_flags = (controls & 4U) != 0 ? 0x10010U : 0U;
                const u32 gamma = (controls & 8U) != 0 ? 12U : 0U;
                registers(system, format | (mode << 8U) | filter_flags | gamma | 64U, x_scales[controls & 3U],
                          y_scales[(controls >> 2U) & 3U], (controls & 1U) != 0);
                equal_fields(system.bus.scan_video(VideoScanMode::Sequential),
                             system.bus.scan_video(VideoScanMode::Parallel));
            }
        }
    }
}

TEST(vi_parallel_scanout_does_not_change_hardware_or_framebuffer_state) {
    System system;
    prepare(system, 0x34971256U);
    registers(system, 0x1001eU, 512, 512, true);
    const auto bytes = system.bus.rdram;
    const auto hidden_span = system.bus.memory.hidden_memory();
    const std::vector<u8> hidden(hidden_span.begin(), hidden_span.end());
    const auto banks = system.bus.memory.bank_status();
    const auto errors = system.bus.memory.errors();
    const u64 clock = system.bus.output_clock();
    const auto first = system.bus.scan_video(VideoScanMode::Parallel);
    equal_fields(first, system.bus.scan_video(VideoScanMode::Parallel));
    equal_fields(first, system.bus.scan_video(VideoScanMode::Sequential));
    CHECK_EQ(system.bus.rdram, bytes);
    CHECK(std::equal(hidden.begin(), hidden.end(), system.bus.memory.hidden_memory().begin()));
    CHECK_EQ(system.bus.memory.bank_status(), banks);
    CHECK_EQ(system.bus.memory.errors(), errors);
    CHECK_EQ(system.bus.output_clock(), clock);
}

TEST(vi_parallel_scanout_keeps_concurrent_machines_independent) {
    std::array<std::future<void>, 4> runs;
    for (unsigned index = 0; index < runs.size(); ++index) {
        runs[index] = std::async(std::launch::async, [index] {
            System system;
            prepare(system, 0x91356872U + index);
            registers(system, 0x1001fU, 512, 768, (index & 1U) != 0);
            const auto expected = system.bus.scan_video(VideoScanMode::Sequential);
            for (unsigned repeat = 0; repeat < 3; ++repeat)
                equal_fields(expected, system.bus.scan_video(VideoScanMode::Parallel));
        });
    }
    for (auto& run : runs)
        run.get();
}
