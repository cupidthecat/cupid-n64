#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <vector>

namespace {
using namespace cupid;

struct OutputFixture {
    System system;

    explicit OutputFixture(VideoStandard region = VideoStandard::Ntsc) : system(region) {
        test::initialize_memory(system);
        system.bus.write(0x04500010, 4, 99);
        system.bus.write(0x04500008, 4, 1);
        for (u32 index = 0; index < 6; ++index)
            system.bus.memory.write(0x2000 + index * 4, 4, ((index + 1) << 16) | (0xffffU - index));
    }
    void queue(u32 address) {
        system.bus.write(0x04500000, 4, address);
        system.bus.write(0x04500004, 4, 8);
    }
    u64 clocks(unsigned samples) const {
        return (samples * 100ULL * 62500000 + system.video_frequency() - 1) / system.video_frequency();
    }
};
} // namespace

TEST(ai_output_observes_consumed_length_and_final_fifo_retirement) {
    for (auto region : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        OutputFixture fixture(region);
        auto& bus = fixture.system.bus;
        fixture.queue(0x2000);
        bus.write(0x0450000c, 4, 0);
        std::vector<std::array<u32, 3>> observations;
        bus.audio_output = [&](s16, s16) {
            observations.push_back({static_cast<u32>(bus.read(0x04500004, 4)),
                                    static_cast<u32>(bus.read(0x0450000c, 4)) & 0xc0000001U,
                                    static_cast<u32>(bus.read(0x04300008, 4)) & 4U});
        };
        bus.tick(fixture.clocks(2));
        CHECK_EQ(observations, (std::vector<std::array<u32, 3>>{{4, 0x40000000, 0}, {0, 0, 0}}));
    }
}

TEST(ai_output_observes_fifo_handoff_before_acknowledging_its_interrupt) {
    OutputFixture fixture;
    auto& bus = fixture.system.bus;
    fixture.queue(0x2000);
    fixture.queue(0x2008);
    bus.write(0x0450000c, 4, 0);
    unsigned samples = 0;
    bus.audio_output = [&](s16, s16) {
        if (++samples != 2)
            return;
        CHECK_EQ(bus.read(0x04500004, 4), 8U);
        CHECK_EQ(bus.read(0x0450000c, 4) & 0xc0000001U, 0x40000000U);
        CHECK_EQ(bus.read(0x04300008, 4) & 4U, 4U);
        bus.write(0x0450000c, 4, 0);
    };
    bus.tick(fixture.clocks(2));
    CHECK_EQ(samples, 2U);
    CHECK_EQ(bus.read(0x04300008, 4) & 4U, 0U);
}

TEST(ai_output_can_refill_the_fifo_slot_freed_by_the_current_sample) {
    for (bool single : {false, true}) {
        OutputFixture fixture;
        auto& bus = fixture.system.bus;
        fixture.queue(0x2000);
        fixture.queue(0x2008);
        std::vector<std::array<s16, 2>> samples;
        bus.audio_output = [&](s16 left, s16 right) {
            samples.push_back({left, right});
            if (samples.size() == 2)
                fixture.queue(0x2010);
        };
        const u64 elapsed = fixture.clocks(6);
        if (single) {
            for (u64 cycle = 0; cycle < elapsed; ++cycle)
                bus.tick(1);
        } else {
            bus.tick(elapsed);
        }
        CHECK_EQ(samples,
                 (std::vector<std::array<s16, 2>>{{1, -1}, {2, -2}, {3, -3}, {4, -4}, {5, -5}, {6, -6}}));
        CHECK_EQ(bus.read(0x04500004, 4), 0U);
        CHECK_EQ(bus.read(0x0450000c, 4) & 0xc0000001U, 0U);
    }
}

TEST(ai_output_can_start_a_new_buffer_after_retiring_the_only_buffer) {
    OutputFixture fixture;
    auto& bus = fixture.system.bus;
    fixture.queue(0x2000);
    std::vector<s16> samples;
    bus.audio_output = [&](s16 left, s16) {
        samples.push_back(left);
        if (samples.size() == 2) {
            CHECK_EQ(bus.read(0x04500004, 4), 0U);
            fixture.queue(0x2010);
            CHECK_EQ(bus.read(0x04500004, 4), 8U);
        }
    };
    bus.tick(fixture.clocks(4));
    CHECK_EQ(samples, (std::vector<s16>{1, 2, 5, 6}));
}
