#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <vector>

namespace {
using namespace cupid;
constexpr u32 Address = 0x04500000;
constexpr u32 Length = 0x04500004;
constexpr u32 Control = 0x04500008;
constexpr u32 Status = 0x0450000c;
constexpr u32 DacRate = 0x04500010;
constexpr u32 MiInterrupt = 0x04300008;

void start(Bus& bus, u32 length) {
    bus.write(DacRate, 4, 1103);
    bus.write(Control, 4, 1);
    bus.write(Address, 4, 0);
    bus.write(Length, 4, length);
    bus.write(Status, 4, 0);
}
} // namespace

TEST(ai_length_counts_stereo_samples_at_the_dac_rate) {
    System system;
    auto& bus = system.bus;
    start(bus, 16);
    bus.tick(1417);
    CHECK_EQ(bus.read(Length, 4), 16U);
    bus.tick(1);
    CHECK_EQ(bus.read(Length, 4), 12U);
    bus.tick(1417);
    CHECK_EQ(bus.read(Length, 4), 8U);
    CHECK_EQ(bus.read(MiInterrupt, 4) & 4U, 0U);
}

TEST(ai_final_buffer_completion_does_not_raise_an_interrupt) {
    System system;
    auto& bus = system.bus;
    start(bus, 8);
    bus.tick(3000);
    CHECK_EQ(bus.read(Length, 4), 0U);
    CHECK_EQ(bus.read(Status, 4) & 0xc0000001U, 0U);
    CHECK_EQ(bus.read(MiInterrupt, 4) & 4U, 0U);
}

TEST(ai_fifo_promotion_raises_an_interrupt_and_preserves_elapsed_time) {
    System system;
    auto& bus = system.bus;
    start(bus, 8);
    bus.write(Address, 4, 0x1000);
    bus.write(Length, 4, 16);
    CHECK_EQ(bus.read(Status, 4) & 0xc0000001U, 0xc0000001U);
    bus.write(Length, 4, 0x1000);
    bus.tick(4253);
    CHECK_EQ(bus.read(Length, 4), 12U);
    CHECK_EQ(bus.read(Status, 4) & 0xc0000001U, 0x40000000U);
    CHECK_EQ(bus.read(MiInterrupt, 4) & 4U, 4U);
    bus.write(Status, 4, 0);
    bus.tick(5000);
    CHECK_EQ(bus.read(Length, 4), 0U);
    CHECK_EQ(bus.read(MiInterrupt, 4) & 4U, 0U);
}

TEST(ai_disabled_dma_retains_data_but_retires_empty_buffers) {
    System system;
    auto& bus = system.bus;
    start(bus, 8);
    bus.write(Control, 4, 0);
    bus.tick(3000);
    CHECK_EQ(bus.read(Length, 4), 8U);
    bus.write(Control, 4, 1);
    bus.tick(1253);
    CHECK_EQ(bus.read(Length, 4), 4U);

    system.reset();
    bus.write(DacRate, 4, 1103);
    bus.write(Length, 4, 0);
    bus.write(Length, 4, 8);
    bus.write(Status, 4, 0);
    bus.tick(1418);
    CHECK_EQ(bus.read(Length, 4), 8U);
    CHECK_EQ(bus.read(Status, 4) & 0xc0000001U, 0x40000000U);
    CHECK_EQ(bus.read(MiInterrupt, 4) & 4U, 4U);
}

TEST(ai_dac_divider_changes_transfer_duration) {
    System system;
    auto& bus = system.bus;
    start(bus, 8);
    bus.write(DacRate, 4, 2207);
    bus.tick(2834);
    CHECK_EQ(bus.read(Length, 4), 8U);
    bus.tick(1);
    CHECK_EQ(bus.read(Length, 4), 4U);
}

TEST(ai_tick_partition_does_not_change_fifo_results) {
    System single;
    System split;
    start(single.bus, 128);
    start(split.bus, 128);
    single.bus.tick(30000);
    for (unsigned index = 0; index < 30000; ++index)
        split.bus.tick(1);
    CHECK_EQ(single.bus.read(Length, 4), 44U);
    CHECK_EQ(single.bus.read(Length, 4), split.bus.read(Length, 4));
    CHECK_EQ(single.bus.read(Status, 4), split.bus.read(Status, 4));
}

TEST(ai_pal_uses_the_pal_video_oscillator) {
    System system(VideoStandard::Pal);
    start(system.bus, 8);
    system.bus.tick(1389);
    CHECK_EQ(system.bus.read(Length, 4), 8U);
    system.bus.tick(1);
    CHECK_EQ(system.bus.read(Length, 4), 4U);
}

TEST(ai_address_carry_crosses_fifo_boundaries_and_full_fifo_writes_are_ignored) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    std::vector<std::array<s16, 2>> samples;
    bus.audio_output = [&](s16 left, s16 right) { samples.push_back({left, right}); };
    bus.write(0x1ff8, 4, 0x80007fff);
    bus.write(0x1ffc, 4, 0x1234fedc);
    bus.write(0x4000, 4, 0x00010002);
    bus.write(0x6000, 4, 0x00030004);
    bus.write(0x6004, 4, 0x00050006);
    bus.write(0x6008, 4, 0x00070008);
    bus.write(0x600c, 4, 0x0009000a);
    bus.write(DacRate, 4, 1103);
    bus.write(Control, 4, 1);
    bus.write(Address, 4, 0x1fff);
    bus.write(Length, 4, 8);
    bus.write(Address, 4, 0x4000);
    bus.write(Length, 4, 8);
    bus.write(Address, 4, 0x1000);
    bus.tick(6000);
    CHECK_EQ(samples.size(), 4U);
    CHECK_EQ(samples[0], (std::array<s16, 2>{-32768, 32767}));
    CHECK_EQ(samples[1], (std::array<s16, 2>{0x1234, -292}));
    CHECK_EQ(samples[2], (std::array<s16, 2>{3, 4}));
    CHECK_EQ(samples[3], (std::array<s16, 2>{5, 6}));

    bus.write(Length, 4, 8);
    bus.tick(3000);
    CHECK_EQ(samples.size(), 6U);
    CHECK_EQ(samples[4], (std::array<s16, 2>{7, 8}));
    CHECK_EQ(samples[5], (std::array<s16, 2>{9, 10}));
}
