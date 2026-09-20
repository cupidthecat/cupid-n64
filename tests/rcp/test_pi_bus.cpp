#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;

void start(Bus& bus, u32 cart, u32 bytes, bool to_dram = true) {
    bus.write(0x04600000, 4, 0x2000);
    bus.write(0x04600004, 4, cart);
    bus.write(to_dram ? 0x0460000cU : 0x04600008U, 4, bytes - 1);
}
} // namespace

TEST(pi_bus_unmapped_cpu_reads_retain_the_address_phase_latch) {
    System system;
    auto& bus = system.bus;
    CHECK_EQ(bus.read(0x05001234, 4), 0x12341234U);
    CHECK_EQ(bus.read(0x04600034, 4), 0x12341234U);
    CHECK_EQ(bus.read(0x04600038, 4), 0x12341234U);
    CHECK_EQ(bus.read(0x05001236, 2), 0x1236U);
    CHECK_EQ(bus.read(0x05001237, 1), 0x36U);
    CHECK_EQ(bus.read(0x04600004, 4), 0x0500123aU);
}

TEST(pi_bus_dma_reselects_unmapped_addresses_only_at_domain_page_boundaries) {
    for (u32 cart : {0x05001234U, 0x06001234U, 0x08001234U, 0x10001234U}) {
        System system;
        test::initialize_memory(system);
        auto& bus = system.bus;
        bus.write(0x0460001c, 4, 2); // Domain 1: 16-byte pages.
        bus.write(0x0460002c, 4, 1); // Domain 2: 8-byte pages.
        start(bus, cart, 24);
        const bool domain2 = cart < 0x06000000U || (cart >= 0x08000000U && cart < 0x10000000U);
        const u32 page_mask = domain2 ? 7U : 15U;
        const u64 deadline = domain2 ? 92U : 78U;
        bus.tick(deadline - 1);
        for (u32 offset = 0; offset < 24; offset += 2)
            CHECK_EQ(bus.read(0x2000 + offset, 2), 0U);
        CHECK_EQ(bus.read(0x04600004, 4), cart + (domain2 ? 20U : 12U));
        bus.tick(1);
        for (u32 offset = 0; offset < 24; offset += 2) {
            const u32 address = cart + offset;
            const u32 selected = (address & ~page_mask) == (cart & ~page_mask) ? cart : address & ~page_mask;
            CHECK_EQ(bus.read(0x2000 + offset, 2), selected & 0xffffU);
        }
        CHECK_EQ(bus.read(0x04600004, 4), cart + 24);
    }
}

TEST(pi_bus_dma_buffer_boundaries_do_not_start_another_address_phase) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.write(0x0460001c, 4, 8); // 1024-byte page, spanning two DMA buffers.
    start(bus, 0x10000120, 256);
    bus.tick(270);
    for (u32 offset = 0; offset < 256; offset += 2)
        CHECK_EQ(bus.read(0x2000 + offset, 2), 0U);
    CHECK_EQ(bus.read(0x04600004, 4), 0x10000120U);
    bus.tick(1);
    for (u32 offset = 0; offset < 128; offset += 2)
        CHECK_EQ(bus.read(0x2000 + offset, 2), 0x0120U);
    for (u32 offset = 128; offset < 256; offset += 2)
        CHECK_EQ(bus.read(0x2000 + offset, 2), 0U);
    CHECK_EQ(bus.read(0x04600004, 4), 0x100001a0U);
    bus.tick(255);
    CHECK_EQ(bus.read(0x2080, 2), 0U);
    bus.tick(1);
    for (u32 offset = 0; offset < 256; offset += 2)
        CHECK_EQ(bus.read(0x2000 + offset, 2), 0x0120U);
    CHECK_EQ(bus.read(0x04600034, 4), 0x01200120U);
}

TEST(pi_bus_rom_exhaustion_retains_the_last_driven_halfword) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.rom = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0xab, 0xcd};
    CHECK_EQ(bus.read(0x10000006, 4), 0xabcdabcdU);
    bus.write(0x0460001c, 4, 2);
    start(bus, 0x10000006, 16);
    bus.tick(61);
    for (u32 offset = 0; offset < 16; offset += 2)
        CHECK_EQ(bus.read(0x2000 + offset, 2), 0U);
    CHECK_EQ(bus.read(0x04600034, 4), 0xabcdabcdU);
    CHECK_EQ(bus.read(0x04600004, 4), 0x10000010U);
    bus.tick(1);
    for (u32 offset = 0; offset < 10; offset += 2)
        CHECK_EQ(bus.read(0x2000 + offset, 2), 0xabcdU);
    for (u32 offset = 10; offset < 16; offset += 2)
        CHECK_EQ(bus.read(0x2000 + offset, 2), 0x0010U);
}

TEST(pi_bus_dma_writes_latch_data_even_without_a_cartridge_device) {
    for (bool sram : {false, true}) {
        System system;
        test::initialize_memory(system);
        auto& bus = system.bus;
        if (sram)
            bus.set_save_type(SaveType::Sram);
        bus.write(0x2000, 4, 0x12345678);
        bus.write(0x2004, 4, 0x9abcdef0);
        start(bus, 0x08000000, 8, false);
        CHECK_EQ(bus.read(0x04600034, 4), 0U);
        if (sram)
            CHECK_EQ(read_be32(bus.sram.data()), 0U);
        bus.tick(46);
        CHECK_EQ(bus.read(0x04600034, 4), 0U);
        bus.tick(1);
        CHECK_EQ(bus.read(0x04600034, 4), 0x56785678U);
        if (sram) {
            CHECK_EQ(read_be32(bus.sram.data()), 0x12345678U);
            CHECK_EQ(read_be32(bus.sram.data() + 4), 0U);
        }
        bus.tick(46);
        CHECK_EQ(bus.read(0x04600034, 4), 0x56785678U);
        bus.tick(1);
        CHECK_EQ(bus.read(0x04600034, 4), 0xdef0def0U);
        CHECK_EQ(bus.read(0x04600038, 4), 0xdef0def0U);
        if (sram)
            CHECK_EQ(read_be32(bus.sram.data() + 4), 0x9abcdef0U);
    }
}

TEST(pi_bus_cpu_stores_preserve_the_full_word_latch) {
    System system;
    auto& bus = system.bus;
    bus.set_save_type(SaveType::Sram);
    bus.write(0x08000000, 4, 0x12345678);
    CHECK_EQ(read_be32(bus.sram.data()), 0x12345678U);
    CHECK_EQ(bus.read(0x04600034, 4), 0x12345678U);
    CHECK_EQ(bus.read(0x05000000, 4), 0x12345678U);
    CHECK_EQ(bus.read(0x04600010, 4) & 2U, 0U);
    CHECK_EQ(bus.read(0x05000000, 4), 0U);
}

TEST(pi_bus_sram_mirroring_applies_at_the_address_phase) {
    for (bool page_crossing : {false, true}) {
        System system;
        test::initialize_memory(system);
        auto& bus = system.bus;
        bus.set_save_type(SaveType::Sram);
        write_be32(bus.sram.data(), 0x12345678);
        write_be32(bus.sram.data() + 0x7ffc, 0xabcdef01);
        bus.write(0x0460002c, 4, page_crossing ? 5 : 15);
        start(bus, 0x08007ffc, 8);
        const u64 deadline = page_crossing ? 46U : 31U;
        bus.tick(deadline - 1);
        CHECK_EQ(bus.read(0x2000, 4), 0U);
        CHECK_EQ(bus.read(0x2004, 4), 0U);
        bus.tick(1);
        CHECK_EQ(bus.read(0x2000, 4), 0xabcdef01U);
        CHECK_EQ(bus.read(0x2004, 4), page_crossing ? 0x12345678U : 0xef01ef01U);
    }
}

TEST(pi_bus_sram_writes_do_not_wrap_without_an_address_phase) {
    for (bool page_crossing : {false, true}) {
        System system;
        test::initialize_memory(system);
        auto& bus = system.bus;
        bus.set_save_type(SaveType::Sram);
        bus.write(0x2000, 4, 0x12345678);
        bus.write(0x2004, 4, 0xabcdef01);
        bus.write(0x0460002c, 4, page_crossing ? 5 : 15);
        start(bus, 0x08007ffc, 8, false);
        if (page_crossing) {
            bus.tick(22);
            CHECK_EQ(read_be32(bus.sram.data() + 0x7ffc), 0U);
            CHECK_EQ(read_be32(bus.sram.data()), 0U);
            bus.tick(1);
            CHECK_EQ(read_be32(bus.sram.data() + 0x7ffc), 0x12345678U);
            CHECK_EQ(read_be32(bus.sram.data()), 0U);
            bus.tick(22);
            CHECK_EQ(read_be32(bus.sram.data()), 0U);
            bus.tick(1);
        } else {
            bus.tick(30);
            CHECK_EQ(read_be32(bus.sram.data() + 0x7ffc), 0U);
            CHECK_EQ(read_be32(bus.sram.data()), 0U);
            bus.tick(1);
        }
        CHECK_EQ(read_be32(bus.sram.data() + 0x7ffc), 0x12345678U);
        CHECK_EQ(read_be32(bus.sram.data()), page_crossing ? 0xabcdef01U : 0U);
    }
}

TEST(pi_bus_banked_sram_decodes_separate_windows_and_holes) {
    System system;
    auto& bus = system.bus;
    bus.set_save_type(SaveType::Sram);
    bus.sram.resize(128 * 1024);
    for (u32 bank = 0; bank < 4; ++bank) {
        const u32 value = 0x12340000U + bank;
        write_be32(bus.sram.data() + bank * 0x8000 + 0x120, value);
        CHECK_EQ(bus.read(0x08000120 + bank * 0x40000, 4), value);
        CHECK_EQ(bus.read(0x08008120 + bank * 0x40000, 4), 0x81208120U);
    }
    CHECK_EQ(bus.read(0x08100120, 4), 0x01200120U);
}
