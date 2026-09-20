#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <vector>

namespace {
using namespace cupid;

constexpr u32 AiAddress = 0x04500000;
constexpr u32 AiLength = 0x04500004;
constexpr u32 AiControl = 0x04500008;
constexpr u32 AiStatus = 0x0450000c;
constexpr u32 AiDacRate = 0x04500010;
constexpr u32 AiBitRate = 0x04500014;
constexpr u32 MiInterrupt = 0x04300008;
constexpr u32 DpClock = 0x04100010;

struct Sample {
    u64 clock;
    s16 left;
    s16 right;
    u32 remaining;
    u32 status;
    u32 interrupt;
};

void check_samples(const std::vector<Sample>& actual, const std::vector<Sample>& expected) {
    CHECK_EQ(actual.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        CHECK_EQ(actual[index].clock, expected[index].clock);
        CHECK_EQ(actual[index].left, expected[index].left);
        CHECK_EQ(actual[index].right, expected[index].right);
        CHECK_EQ(actual[index].remaining, expected[index].remaining);
        CHECK_EQ(actual[index].status, expected[index].status);
        CHECK_EQ(actual[index].interrupt, expected[index].interrupt);
    }
}

void write_pcm(Bus& bus, u32 address, u32 word) {
    bus.memory.write(address, 4, word);
}

void queue(Bus& bus, u32 address, u32 length) {
    bus.write(AiAddress, 4, address);
    bus.write(AiLength, 4, length);
}

u32 read32(Bus& bus, u32 address) {
    return static_cast<u32>(bus.read(address, 4));
}

void configure_video_boundary(Bus& bus) {
    bus.write(0x04400000, 4, 0x303);
    bus.write(0x04400008, 4, 16);
    bus.write(0x04400018, 4, 525);
    bus.write(0x0440001c, 4, 99);
    bus.write(0x04400020, 4, (100U << 16) | 100U);
    bus.write(0x04400024, 4, (108U << 16) | 125U);
    bus.write(0x04400028, 4, (34U << 16) | 36U);
    bus.write(0x04400030, 4, 1024);
    bus.write(0x04400034, 4, 1024);
}

std::vector<std::array<s16, 2>> run_bitrate_fixture(u32 bit_rate) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    write_pcm(bus, 0x6000, 0x00ff8001);
    write_pcm(bus, 0x6004, 0x7f00ff7f);
    std::vector<std::array<s16, 2>> samples;
    bus.audio_output = [&](s16 left, s16 right) { samples.push_back({left, right}); };
    bus.write(AiDacRate, 4, 1103);
    bus.write(AiBitRate, 4, bit_rate);
    bus.write(AiControl, 4, 1);
    queue(bus, 0x6000, 8);
    bus.write(AiStatus, 4, 0);
    bus.tick(2835);
    return samples;
}
} // namespace

TEST(ai_conformance_raw_fifo_fixture_matches_literal_pcm_and_deadlines) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;

    write_pcm(bus, 0x2000, 0x80017fff);
    write_pcm(bus, 0x2004, 0xffff0001);
    write_pcm(bus, 0x3000, 0x7fff8000);
    write_pcm(bus, 0x3004, 0x1234fedc);
    write_pcm(bus, 0x4000, 0x0001fffe);
    write_pcm(bus, 0x4004, 0x7ffe8002);

    std::vector<Sample> samples;
    bus.audio_output = [&](s16 left, s16 right) {
        samples.push_back({bus.read(DpClock, 4), left, right, read32(bus, AiLength),
                           read32(bus, AiStatus) & 0xc0000001U, read32(bus, MiInterrupt) & 4U});
    };

    bus.write(AiDacRate, 4, 131);
    bus.write(AiBitRate, 4, 0xfffffff1U);
    bus.write(AiControl, 4, 1);
    queue(bus, 0x2007, 0xf);
    queue(bus, 0x3007, 0xf);
    bus.write(AiStatus, 4, 0);
    queue(bus, 0x4000, 8);

    bus.tick(678);
    check_samples(samples, {{170, -32767, 32767, 4, 0xc0000001U, 0},
                            {339, -1, 1, 8, 0x40000000U, 4},
                            {509, 32767, -32768, 4, 0x40000000U, 4},
                            {678, 4660, -292, 0, 0, 4}});
    CHECK_EQ(bus.read(AiLength, 4), 0U);
    CHECK_EQ(bus.read(AiStatus, 4) & 0xc0000001U, 0U);

    bus.tick(170);
    CHECK_EQ(samples.size(), 4U);
    bus.write(AiStatus, 4, 0);
    queue(bus, 0x4007, 0xf);
    bus.write(AiStatus, 4, 0);
    bus.tick(339);

    check_samples(samples, {{170, -32767, 32767, 4, 0xc0000001U, 0},
                            {339, -1, 1, 8, 0x40000000U, 4},
                            {509, 32767, -32768, 4, 0x40000000U, 4},
                            {678, 4660, -292, 0, 0, 4},
                            {1017, 1, -2, 4, 0x40000000U, 0},
                            {1187, 32766, -32766, 0, 0, 0}});
}

TEST(ai_conformance_bitrate_does_not_truncate_signed_pcm_words) {
    const std::vector<std::array<s16, 2>> expected{{255, -32767}, {32512, -129}};
    for (u32 bit_rate : {1U, 7U, 15U, 0xffffffffU})
        CHECK_EQ(run_bitrate_fixture(bit_rate), expected);
}

TEST(ai_conformance_dac_boundaries_match_literal_ntsc_and_pal_clocks) {
    struct Case {
        VideoStandard region;
        u32 divider;
        u32 bit_rate;
        u64 first;
        u64 second;
    };
    constexpr std::array<Case, 6> cases{{
        {VideoStandard::Ntsc, 131, 1, 170, 339},
        {VideoStandard::Pal, 131, 1, 167, 333},
        {VideoStandard::Ntsc, 1103, 15, 1418, 2835},
        {VideoStandard::Pal, 1103, 15, 1390, 2780},
        {VideoStandard::Ntsc, 16383, 15, 21035, 42070},
        {VideoStandard::Pal, 16383, 15, 20622, 41244},
    }};
    for (const auto& entry : cases) {
        System system(entry.region);
        test::initialize_memory(system);
        auto& bus = system.bus;
        write_pcm(bus, 0x6000, 0x00ff8001);
        write_pcm(bus, 0x6004, 0x7f00ff7f);
        std::vector<u64> clocks;
        bus.audio_output = [&](s16, s16) { clocks.push_back(bus.read(DpClock, 4)); };
        bus.write(AiDacRate, 4, entry.divider);
        bus.write(AiBitRate, 4, entry.bit_rate);
        bus.write(AiControl, 4, 1);
        queue(bus, 0x6000, 8);
        bus.write(AiStatus, 4, 0);
        bus.tick(entry.second - 1);
        CHECK_EQ(clocks, (std::vector<u64>{entry.first}));
        bus.tick(1);
        CHECK_EQ(clocks, (std::vector<u64>{entry.first, entry.second}));
    }
}

TEST(ai_conformance_coincident_video_edge_queues_for_the_following_dac_period) {
    for (bool single_cycle : {false, true}) {
        System system;
        test::initialize_memory(system);
        auto& bus = system.bus;
        configure_video_boundary(bus);
        write_pcm(bus, 0x7000, 0x1111eeee);
        write_pcm(bus, 0x7004, 0x80007fff);

        bus.write(AiDacRate, 4, 1699);
        bus.write(AiBitRate, 4, 1);
        bus.write(AiControl, 4, 1);
        std::vector<Sample> samples;
        bus.audio_output = [&](s16 left, s16 right) {
            samples.push_back({bus.read(DpClock, 4), left, right, read32(bus, AiLength),
                               read32(bus, AiStatus) & 0xc0000001U, read32(bus, MiInterrupt) & 4U});
        };

        unsigned video_callbacks = 0;
        bus.set_video_output([&](VideoField) {
            ++video_callbacks;
            CHECK_EQ(bus.read(DpClock, 4), 2183U);
            CHECK(samples.empty());
            queue(bus, 0x7000, 8);
            bus.write(AiStatus, 4, 0);
            bus.write(AiDacRate, 4, 131);
            bus.set_video_output({});
        });

        if (single_cycle) {
            for (u64 cycle = 0; cycle < 2522; ++cycle)
                bus.tick(1);
        } else {
            bus.tick(2522);
        }
        CHECK_EQ(video_callbacks, 1U);
        check_samples(samples, {{2353, 4369, -4370, 4, 0x40000000U, 0}, {2522, -32768, 32767, 0, 0, 0}});
    }
}
