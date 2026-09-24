#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <memory>

namespace {
using namespace cupid;

constexpr std::array<u16, 8> values{0x8040U, 0x2010U, 0xa55aU, 0xc33cU, 0x1ffeU, 0xf00fU, 0xffffU, 0x1357U};
constexpr u32 source = 0x1ff8U;

void initialize(System& system) {
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.set_save_type(SaveType::Sram);
    // Two eight-byte pages retire at RCP clocks 51 and 102.
    bus.write(0x0460002c, 4, 1);
    for (unsigned index = 0; index < values.size(); ++index)
        bus.memory.write(source + index * 2U, 2, values[index]);
}

void start(Bus& bus, u32 address = source, u32 bytes = 16) {
    bus.write(0x04600000, 4, address);
    bus.write(0x04600004, 4, 0x08000000);
    bus.write(0x04600008, 4, bytes - 1U);
}

void calibrate(Rdram& memory) {
    constexpr std::array<unsigned, 6> positions{6, 14, 22, 7, 15, 23};
    constexpr unsigned encoded = 11U ^ 63U;
    u32 control = 0x02000000U;
    for (unsigned bit = 0; bit < positions.size(); ++bit)
        control |= ((encoded >> bit) & 1U) << positions[bit];
    memory.write_register(0x03f0000c, control);
}

void tick(Bus& bus, u64 cycles, bool split) {
    if (split) {
        for (u64 cycle = 0; cycle < cycles; ++cycle)
            bus.tick(1);
    } else {
        bus.tick(cycles);
    }
}
} // namespace

TEST(pi_memory_calibration_reads_one_halfword_per_cartridge_transfer) {
    for (bool split : {false, true}) {
        auto system = std::make_unique<System>();
        auto halfwords = std::make_unique<System>();
        for (auto* instance : {system.get(), halfwords.get()}) {
            initialize(*instance);
            calibrate(instance->bus.memory);
            CHECK(!instance->bus.memory.direct_access_ready());
        }
        std::array<u16, values.size()> expected{};
        for (unsigned index = 0; index < expected.size(); ++index)
            expected[index] = static_cast<u16>(halfwords->bus.memory.read(source + index * 2U, 2));
        CHECK(expected != values);

        auto& bus = system->bus;
        start(bus);
        tick(bus, 50, split);
        CHECK(std::all_of(bus.sram.begin(), bus.sram.begin() + 16, [](u8 byte) { return byte == 0; }));
        tick(bus, 1, split);
        for (unsigned index = 0; index < 4; ++index)
            CHECK_EQ(read_be16(bus.sram.data() + index * 2U), expected[index]);
        CHECK_EQ(bus.memory.bank_access_clock(source), 51U);
        CHECK_EQ(bus.read(0x04600010, 4), 1U);
        CHECK_EQ(bus.read(0x04300008, 4) & 0x10U, 0U);
        tick(bus, 51, split);
        for (unsigned index = 0; index < expected.size(); ++index)
            CHECK_EQ(read_be16(bus.sram.data() + index * 2U), expected[index]);
        CHECK_EQ(bus.memory.bank_access_clock(source + 8), 102U);
        CHECK_EQ(bus.read(0x04600010, 4), 8U);
        CHECK_EQ(bus.read(0x04300008, 4) & 0x10U, 0x10U);
        CHECK_EQ(bus.memory.errors(), 0U);
        CHECK_EQ(bus.rdram, halfwords->bus.rdram);
        CHECK_EQ(bus.memory.read(source, 2), halfwords->bus.memory.read(source, 2));
    }
}

TEST(pi_memory_halfwords_observe_chip_mapping_and_unavailable_memory) {
    for (unsigned mode = 0; mode < 4; ++mode) {
        auto system = std::make_unique<System>();
        auto halfwords = std::make_unique<System>();
        const u32 base = mode == 1 ? 0x00a00000U : mode == 3 ? 0x00800000U : 0U;
        for (auto* instance : {system.get(), halfwords.get()}) {
            initialize(*instance);
            if (mode == 1)
                instance->bus.memory.write_register(0x03f00004, 10U << 26U);
            if (mode == 2)
                instance->bus.memory.set_bus_active(false);
        }
        std::array<u16, values.size()> expected{};
        for (unsigned index = 0; index < expected.size(); ++index)
            expected[index] = static_cast<u16>(halfwords->bus.memory.read(base + source + index * 2U, 2));
        CHECK_EQ(expected, mode < 2 ? values : (std::array<u16, values.size()>{}));
        auto& bus = system->bus;
        start(bus, base + source);
        bus.tick(102);
        for (unsigned index = 0; index < expected.size(); ++index)
            CHECK_EQ(read_be16(bus.sram.data() + index * 2U), expected[index]);
        CHECK_EQ(bus.memory.errors(), halfwords->bus.memory.errors());
        CHECK_EQ(bus.memory.bank_status(), halfwords->bus.memory.bank_status());
        CHECK_EQ(bus.rdram, halfwords->bus.rdram);
        CHECK_EQ(bus.read(0x04600010, 4), 8U);
    }
}

TEST(pi_memory_rejects_a_halfword_crossing_the_backing_extent) {
    auto system = std::make_unique<System>();
    initialize(*system);
    auto& bus = system->bus;
    bus.rdram.resize(source + 1U);
    CHECK_EQ(bus.rdram[source], 0x80U);
    CHECK_EQ(bus.memory.read(source, 2), 0U);
    start(bus, source, 2);
    bus.tick(18);
    CHECK_EQ(bus.read(0x04600010, 4), 1U);
    bus.tick(1);
    CHECK_EQ(read_be16(bus.sram.data()), 0U);
    CHECK_EQ(bus.memory.errors(), 0U);
    CHECK_EQ(bus.read(0x04600034, 4), 0U);
    CHECK_EQ(bus.read(0x04600010, 4), 8U);
    CHECK_EQ(bus.rdram[source], 0x80U);
}

TEST(pi_memory_halfwords_sample_live_pages_without_cpu_ebus_lane_selection) {
    auto system = std::make_unique<System>();
    initialize(*system);
    auto& bus = system->bus;
    bus.write(0x04300000, 4, 0x400); // CPU EBUS mode must not redirect PI reads to hidden bits.
    CHECK_EQ(bus.read(0x04300000, 4) & 0x100U, 0x100U);
    CHECK_EQ(bus.read(source, 4), 0U);
    start(bus);
    bus.tick(51);
    for (unsigned index = 0; index < 4; ++index)
        CHECK_EQ(read_be16(bus.sram.data() + index * 2U), values[index]);
    for (unsigned index = 4; index < values.size(); ++index)
        bus.memory.write(source + index * 2U, 2, values[index] ^ 0xffffU);
    bus.tick(51);
    for (unsigned index = 0; index < values.size(); ++index)
        CHECK_EQ(read_be16(bus.sram.data() + index * 2U),
                 static_cast<u16>(index < 4 ? values[index] : values[index] ^ 0xffffU));
    CHECK_EQ(bus.read(0x04600010, 4), 8U);
}
