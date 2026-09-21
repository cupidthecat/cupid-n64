#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <functional>
#include <utility>
#include <vector>

namespace {
using namespace cupid;

struct BoundaryFixture {
    System system;
    bool cpu;
    bool single;
    u64 elapsed{};
    unsigned callbacks{};

    BoundaryFixture(bool through_cpu, bool one_cycle, VideoStandard region = VideoStandard::Ntsc)
        : system(region), cpu(through_cpu), single(one_cycle) {
        test::initialize_memory(system);
        const auto vi = [&](u32 index, u32 value) { system.bus.write(0x04400000 + index * 4, 4, value); };
        vi(0, 0x303);
        vi(2, 16);
        vi(6, 525);
        vi(7, 99);
        vi(8, (100U << 16) | 100U);
        vi(9, (108U << 16) | 125U);
        vi(10, (34U << 16) | 36U);
        vi(12, 1024);
        vi(13, 1024);
    }
    u64 time(u64 video_clocks = 1700) const {
        return (video_clocks * 62500000 + system.video_frequency() - 1) / system.video_frequency();
    }
    void until(u64 rcp_clock) {
        const u64 target = cpu ? (rcp_clock * 3 + 1) / 2 : rcp_clock;
        while (elapsed < target) {
            const u64 amount = single ? 1 : target - elapsed;
            if (cpu)
                system.advance(amount);
            else
                system.bus.tick(amount);
            elapsed += amount;
        }
    }
    void video(std::function<void()> start) {
        system.bus.set_video_output([&, start = std::move(start)](VideoField) {
            ++callbacks;
            CHECK_EQ(system.bus.read(0x04100010, 4), time());
            start();
            system.bus.set_video_output({});
        });
    }
};
} // namespace

TEST(output_boundary_preserves_new_pi_dma_deadlines) {
    for (bool cpu : {false, true})
        for (bool single : {false, true})
            for (auto region : {VideoStandard::Ntsc, VideoStandard::Pal}) {
                BoundaryFixture fixture(cpu, single, region);
                auto& bus = fixture.system.bus;
                bus.write(0x04600014, 4, 3);
                bus.write(0x04600018, 4, 5);
                bus.write(0x0460001c, 4, 5);
                bus.write(0x04600020, 4, 1);
                fixture.video([&] {
                    bus.write(0x04600000, 4, 0x2000);
                    bus.write(0x04600004, 4, 0x10000000);
                    bus.write(0x0460000c, 4, 7);
                });
                fixture.until(fixture.time());
                CHECK_EQ(fixture.callbacks, 1U);
                CHECK_EQ(bus.read(0x04600010, 4), 1U);
                fixture.until(fixture.time() + 57);
                CHECK_EQ(bus.read(0x04600010, 4), 1U);
                CHECK_EQ(bus.read(0x04300008, 4) & 16U, 0U);
                fixture.until(fixture.time() + 58);
                CHECK_EQ(bus.read(0x04600010, 4), 8U);
                CHECK_EQ(bus.read(0x04300008, 4) & 16U, 16U);
            }
}

TEST(output_boundary_preserves_new_pi_and_si_io_deadlines) {
    for (bool cpu : {false, true})
        for (bool single : {false, true}) {
            BoundaryFixture fixture(cpu, single);
            auto& bus = fixture.system.bus;
            fixture.video([&] {
                bus.write(0x10000000, 4, 0x12345678);
                bus.write(0x1fc007c0, 4, 0xfe000000);
            });
            fixture.until(fixture.time() + 139);
            CHECK_EQ(bus.read(0x04600010, 4) & 2U, 2U);
            fixture.until(fixture.time() + 140);
            CHECK_EQ(bus.read(0x04600010, 4) & 2U, 0U);
            fixture.until(fixture.time() + 2149);
            CHECK_EQ(bus.read(0x04800018, 4) & 0x1003U, 3U);
            fixture.until(fixture.time() + 2150);
            CHECK_EQ(bus.read(0x04800018, 4) & 0x1003U, 0x1000U);
        }
}

TEST(output_boundary_preserves_new_si_dma_payload_and_deadline) {
    for (bool cpu : {false, true})
        for (bool single : {false, true}) {
            BoundaryFixture fixture(cpu, single);
            auto& bus = fixture.system.bus;
            bus.memory.write(0x2000, 4, 0xfe123456);
            fixture.video([&] {
                bus.write(0x04800000, 4, 0x2000);
                bus.write(0x04800010, 4, 0x1fc007c0);
            });
            fixture.until(fixture.time() + 4064);
            CHECK_EQ(bus.read(0x04800018, 4) & 0x1001U, 1U);
            CHECK_EQ(read_be32(bus.pif.data() + 0x7c0), 0U);
            fixture.until(fixture.time() + 4065);
            CHECK_EQ(bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
            CHECK_EQ(read_be32(bus.pif.data() + 0x7c0), 0xfe123456U);
        }
}

TEST(output_boundary_preserves_sp_dma_started_by_video_or_audio) {
    for (bool audio : {false, true})
        for (bool single : {false, true}) {
            BoundaryFixture fixture(true, single);
            auto& system = fixture.system;
            auto& bus = system.bus;
            bus.write(0x04000000, 4, 0x12345678);
            const auto start = [&] {
                system.rsp.write_register(0, 0);
                system.rsp.write_register(4, 0x2000);
                system.rsp.write_register(0x0c, 7);
            };
            if (audio) {
                bus.write(0x04500010, 4, 1699);
                bus.write(0x04500008, 4, 1);
                bus.write(0x04500004, 4, 8);
                bus.audio_output = [&](s16, s16) { start(); };
            } else {
                fixture.video(start);
            }
            fixture.until(fixture.time());
            CHECK_EQ(system.rsp.read_register(0x18), 1U);
            CHECK_EQ(bus.memory.read(0x2000, 4), 0U);
            fixture.until(fixture.time() + 1);
            CHECK_EQ(system.rsp.read_register(0x18), 0U);
            CHECK_EQ(bus.memory.read(0x2000, 4), 0x12345678U);
        }
}

TEST(output_boundary_does_not_consume_new_audio_at_a_coincident_dac_edge) {
    for (bool cpu : {false, true})
        for (bool single : {false, true}) {
            BoundaryFixture fixture(cpu, single);
            auto& bus = fixture.system.bus;
            bus.write(0x04500010, 4, 1699);
            bus.write(0x04500008, 4, 1);
            std::vector<u64> samples;
            bus.audio_output = [&](s16, s16) { samples.push_back(bus.read(0x04100010, 4)); };
            fixture.video([&] {
                bus.write(0x04500004, 4, 8);
                bus.write(0x04500010, 4, 99);
            });
            fixture.until(fixture.time());
            CHECK(samples.empty());
            fixture.until(fixture.time(1900));
            CHECK_EQ(samples, (std::vector<u64>{fixture.time(1800), fixture.time(1900)}));
        }
}

TEST(output_boundary_observes_pi_and_sp_completions_at_the_same_clock) {
    for (bool single : {false, true}) {
        BoundaryFixture fixture(true, single);
        auto& system = fixture.system;
        auto& bus = system.bus;
        bus.write(0x04600014, 4, 3);
        bus.write(0x04600018, 4, 5);
        bus.write(0x0460001c, 4, 5);
        bus.write(0x04600020, 4, 1);
        bus.write(0x04000000, 4, 0x12345678);
        fixture.video([&] {
            CHECK_EQ(bus.read(0x04600010, 4), 8U);
            CHECK_EQ(bus.read(0x04300008, 4) & 16U, 16U);
            CHECK_EQ(system.rsp.read_register(0x18), 0U);
            CHECK_EQ(bus.memory.read(0x2000, 4), 0x12345678U);
        });
        fixture.until(fixture.time() - 58);
        bus.write(0x04600000, 4, 0x3000);
        bus.write(0x04600004, 4, 0x10000000);
        bus.write(0x0460000c, 4, 7);
        fixture.until(fixture.time() - 1);
        system.rsp.write_register(0, 0);
        system.rsp.write_register(4, 0x2000);
        system.rsp.write_register(0x0c, 7);
        fixture.until(fixture.time());
        CHECK_EQ(fixture.callbacks, 1U);
    }
}

TEST(output_boundary_reset_discards_undelivered_samples) {
    for (bool cpu : {false, true}) {
        BoundaryFixture fixture(cpu, false);
        auto& bus = fixture.system.bus;
        bus.write(0x04500010, 4, 1699);
        bus.write(0x04500008, 4, 1);
        bus.write(0x04500004, 4, 8);
        unsigned samples = 0;
        bus.audio_output = [&](s16, s16) { ++samples; };
        fixture.video([&] { fixture.system.reset(); });
        fixture.until(fixture.time());
        CHECK_EQ(fixture.callbacks, 1U);
        CHECK_EQ(samples, 0U);
        CHECK_EQ(bus.read(0x04300008, 4), 0U);
        CHECK_EQ(bus.read(0x04500004, 4), 0U);
        bus.write(0x04400000, 4, 1);
        bus.write(0x04400018, 4, 525);
        bus.write(0x0440001c, 4, 9);
        bus.write(0x04400020, 4, (10U << 16) | 10U);
        bus.tick(fixture.time(10));
        CHECK_EQ(bus.read(0x04400010, 4), 2U);
    }
}

TEST(output_boundary_keeps_buffered_cpu_rate_writes_after_the_dac_latch) {
    BoundaryFixture fixture(false, false);
    auto& system = fixture.system;
    auto& bus = system.bus;
    bus.write(0x1000, 4, 0xac220000);
    system.cpu.write_cop0(12, 0x34000000);
    system.cpu.set_pc(0xffffffff80001000ULL);
    system.cpu.gpr[1] = 0xffffffffa4500010ULL;
    system.cpu.gpr[2] = 19;
    u64 instruction = 0;
    CHECK(system.cpu.read_memory(0xffffffff80001000ULL, 4, instruction, true));
    bus.write(0x04500010, 4, 99);
    bus.write(0x04500008, 4, 1);
    bus.write(0x04500004, 4, 16);
    std::vector<u64> samples;
    bus.audio_output = [&](s16, s16) { samples.push_back(bus.read(0x04100010, 4)); };
    fixture.until(fixture.time(100) - 2);
    system.cpu.step();
    CHECK_EQ(bus.read(0x04100010, 4), fixture.time(100) - 2);
    system.advance(3);
    CHECK_EQ(bus.read(0x04100010, 4), fixture.time(100));
    bus.tick(fixture.time(240) - fixture.time(100));
    CHECK_EQ(samples,
             (std::vector<u64>{fixture.time(100), fixture.time(200), fixture.time(220), fixture.time(240)}));
}
