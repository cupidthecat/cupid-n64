#include "cupid/system.hpp"
#include "test.hpp"

namespace {
using namespace cupid;

constexpr u32 Mode = 0x04300000U;
constexpr u32 Version = 0x04300004U;
constexpr u32 Interrupt = 0x04300008U;
constexpr u32 Mask = 0x0430000cU;
} // namespace

TEST(mi_mode_set_clear_pairs_apply_in_bus_order_and_ignore_reserved_bits) {
    System system;
    auto& bus = system.bus;

    bus.write(Mode, 4, 0x55U | (1U << 8U) | (1U << 10U) | (1U << 13U));
    CHECK_EQ(bus.read(Mode, 4), 0x3d5U);

    bus.write(Mode, 4,
              0xffffc000U | 0x2aU | (1U << 7U) | (1U << 8U) | (1U << 9U) | (1U << 10U) | (1U << 12U) |
                  (1U << 13U));
    CHECK_EQ(bus.read(Mode, 4), 0x3aaU);

    bus.write(Mode, 4, 0x1bU | (1U << 7U) | (1U << 9U) | (1U << 12U));
    CHECK_EQ(bus.read(Mode, 4), 0x1bU);

    CHECK_EQ(bus.read(Version, 4), 0x02020102U);
    bus.write(Version, 4, 0xffffffffU);
    CHECK_EQ(bus.read(Version, 4), 0x02020102U);

    bus.set_interrupt(0, true);
    CHECK_EQ(bus.read(Interrupt, 4), 1U);
    bus.write(Interrupt, 4, 0xffffffffU);
    CHECK_EQ(bus.read(Interrupt, 4), 1U);
}

TEST(mi_register_mirrors_and_subword_lanes_use_the_rcp_word_bus) {
    System system;
    auto& bus = system.bus;

    for (u32 address : {Version, 0x04310004U, 0x043ffff4U}) {
        CHECK_EQ(bus.read(address, 4), 0x02020102U);
        CHECK_EQ(bus.read(address + 0, 2), 0x0202U);
        CHECK_EQ(bus.read(address + 2, 2), 0x0102U);
        CHECK_EQ(bus.read(address + 0, 1), 0x02U);
        CHECK_EQ(bus.read(address + 1, 1), 0x02U);
        CHECK_EQ(bus.read(address + 2, 1), 0x01U);
        CHECK_EQ(bus.read(address + 3, 1), 0x02U);
    }

    bus.write(Mask + 3, 1, 0x02);
    CHECK_EQ(bus.read(Mask, 4), 0x01U);
    bus.write(Mask + 2, 1, 0x02);
    CHECK_EQ(bus.read(Mask, 4), 0x11U);
    bus.write(Mask + 2, 1, 0x01);
    CHECK_EQ(bus.read(Mask, 4), 0x01U);

    bus.write(Mask + 0, 1, 0xff);
    bus.write(Mask + 1, 1, 0xff);
    CHECK_EQ(bus.read(Mask, 4), 0x01U);
    bus.write(Mask + 2, 2, 0x0020);
    CHECK_EQ(bus.read(Mask, 4), 0x05U);
    bus.write(Mask + 0, 2, 0xffff);
    CHECK_EQ(bus.read(Mask, 4), 0x05U);

    bus.write(Mask + 3, 1, 0x01);
    CHECK_EQ(bus.read(Mask, 4), 0x04U);
    bus.write(0x043fffffU, 1, 0x02);
    CHECK_EQ(bus.read(Mask, 4), 0x05U);
}

TEST(mi_simultaneous_interrupt_sources_and_masks_remain_independent) {
    System system;
    auto& bus = system.bus;

    for (unsigned source = 0; source < 6; ++source)
        bus.set_interrupt(source, true);
    CHECK_EQ(bus.read(Interrupt, 4), 0x3fU);
    CHECK(!bus.interrupt_pending());

    bus.write(Mask, 4, 0xaaaU);
    CHECK_EQ(bus.read(Mask, 4), 0x3fU);
    CHECK(bus.interrupt_pending());

    bus.write(Mask, 4, 1U << 2U); // Mask SI while its line stays asserted.
    CHECK_EQ(bus.read(Mask, 4), 0x3dU);
    bus.write(Mask, 4, (1U << 8U) | (1U << 9U)); // Clear+set PI: set wins.
    CHECK_EQ(bus.read(Mask, 4), 0x3dU);

    for (unsigned source : {0U, 2U, 3U, 4U})
        bus.set_interrupt(source, false);
    CHECK_EQ(bus.read(Interrupt, 4), 0x22U);
    CHECK(bus.interrupt_pending());

    bus.write(Mode, 4, 1U << 11U); // MI_MODE clears DP only.
    CHECK_EQ(bus.read(Interrupt, 4), 0x02U);
    CHECK(!bus.interrupt_pending());
    CHECK_EQ(bus.read(Mask, 4), 0x3dU);

    bus.write(Mask, 4, 1U << 3U); // Unmask the still-asserted SI source.
    CHECK_EQ(bus.read(Mask, 4), 0x3fU);
    CHECK(bus.interrupt_pending());
    bus.set_interrupt(1, false);
    CHECK(!bus.interrupt_pending());
}
