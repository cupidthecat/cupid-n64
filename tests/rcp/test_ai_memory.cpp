#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <array>
#include <memory>
#include <vector>

namespace {
using namespace cupid;

constexpr std::array<u32, 4> addresses{0x1ff8U, 0x1ffcU, 0x2000U, 0x2004U};
constexpr std::array<u32, 4> words{0x80402010U, 0xa55ac33cU, 0x1ffef00fU, 0xffffffffU};

void initialize(System& system) {
    test::initialize_memory(system);
    for (unsigned index = 0; index < words.size(); ++index)
        system.bus.memory.write(addresses[index], 4, words[index]);
    system.bus.write(0x04500010, 4, 99);
    system.bus.write(0x04500008, 4, 1);
}

u64 deadline(const System& system, unsigned samples) {
    return (samples * 100ULL * 62500000 + system.video_frequency() - 1) / system.video_frequency();
}

void queue(Bus& bus, u32 address, u32 length) {
    bus.write(0x04500000, 4, address);
    bus.write(0x04500004, 4, length);
    bus.write(0x0450000c, 4, 0);
}

u32 stereo_word(s16 left, s16 right) {
    return (static_cast<u32>(static_cast<u16>(left)) << 16U) | static_cast<u16>(right);
}

void calibrate(Rdram& memory) {
    constexpr std::array<unsigned, 6> positions{6, 14, 22, 7, 15, 23};
    constexpr unsigned encoded = 11U ^ 63U;
    u32 control = 0x02000000U;
    for (unsigned bit = 0; bit < positions.size(); ++bit)
        control |= ((encoded >> bit) & 1U) << positions[bit];
    memory.write_register(0x03f0000c, control);
}
} // namespace

TEST(ai_memory_calibration_applies_to_each_complete_stereo_word) {
    for (const auto region : {VideoStandard::Ntsc, VideoStandard::Pal}) {
        auto system = std::make_unique<System>(region);
        auto word_reads = std::make_unique<System>(region);
        for (auto* instance : {system.get(), word_reads.get()}) {
            initialize(*instance);
            calibrate(instance->bus.memory);
            CHECK(!instance->bus.memory.direct_access_ready());
        }
        std::vector<u32> expected;
        for (const u32 address : addresses)
            expected.push_back(static_cast<u32>(word_reads->bus.memory.read(address, 4)));
        CHECK(expected != std::vector<u32>(words.begin(), words.end()));

        std::vector<u32> actual;
        auto& bus = system->bus;
        bus.audio_output = [&](s16 left, s16 right) { actual.push_back(stereo_word(left, right)); };
        queue(bus, addresses[0], 16);
        bus.tick(deadline(*system, 4));
        CHECK_EQ(actual, expected);
        CHECK_EQ(bus.read(0x04500004, 4), 0U);
        CHECK_EQ(bus.read(0x0450000c, 4) & 0xc0000001U, 0U);
        CHECK_EQ(bus.read(0x04300008, 4) & 4U, 0U);
        CHECK(bus.memory.row_open(addresses.back()));
        CHECK_EQ(bus.memory.bank_access_clock(addresses.back()), deadline(*system, 4));
        CHECK_EQ(bus.memory.errors(), 0U);
        CHECK_EQ(bus.rdram, word_reads->bus.rdram);
        CHECK_EQ(bus.memory.read(addresses[0], 4), word_reads->bus.memory.read(addresses[0], 4));
    }
}

TEST(ai_memory_word_reads_preserve_remapping_and_unavailable_memory) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        auto system = std::make_unique<System>();
        auto word_reads = std::make_unique<System>();
        const u32 base = mode == 1 ? 0x00a00000U : mode == 3 ? 0x00800000U : 0U;
        for (auto* instance : {system.get(), word_reads.get()}) {
            initialize(*instance);
            if (mode == 1)
                instance->bus.memory.write_register(0x03f00004, 10U << 26U);
            if (mode == 2)
                instance->bus.memory.set_bus_active(false);
        }
        std::vector<u32> expected;
        for (const u32 address : addresses)
            expected.push_back(static_cast<u32>(word_reads->bus.memory.read(base + address, 4)));
        CHECK_EQ(expected, mode < 2 ? std::vector<u32>(words.begin(), words.end()) : std::vector<u32>(4, 0));
        std::vector<u32> actual;
        auto& bus = system->bus;
        bus.audio_output = [&](s16 left, s16 right) { actual.push_back(stereo_word(left, right)); };
        queue(bus, base + addresses[0], 16);
        bus.tick(deadline(*system, 4));
        CHECK_EQ(actual, expected);
        CHECK_EQ(bus.memory.errors(), word_reads->bus.memory.errors());
        CHECK_EQ(bus.memory.bank_status(), word_reads->bus.memory.bank_status());
        CHECK_EQ(bus.read(0x04500004, 4), 0U);
        CHECK_EQ(bus.read(0x0450000c, 4) & 0xc0000001U, 0U);
        CHECK_EQ(bus.rdram, word_reads->bus.rdram);
    }
}

TEST(ai_memory_rejects_a_stereo_word_crossing_the_backing_extent) {
    auto system = std::make_unique<System>();
    initialize(*system);
    auto& bus = system->bus;
    bus.rdram.resize(0x2002);
    CHECK_EQ(bus.memory.read(0x2000, 4), 0U);
    std::vector<u32> actual;
    bus.audio_output = [&](s16 left, s16 right) { actual.push_back(stereo_word(left, right)); };
    queue(bus, 0x2000, 8);
    bus.tick(deadline(*system, 1));
    CHECK_EQ(actual, (std::vector<u32>{0}));
    CHECK_EQ(bus.read(0x04500004, 4), 4U);
    CHECK_EQ(bus.read(0x0450000c, 4) & 0xc0000001U, 0x40000000U);
    CHECK_EQ(bus.memory.errors(), 0U);
    CHECK_EQ(bus.rdram[0x2000], 0x1fU);
    CHECK_EQ(bus.rdram[0x2001], 0xfeU);
}
