#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

namespace {
using namespace cupid;

void configure_domain(Bus& bus, unsigned domain, u32 latency, u32 pulse, u32 page, u32 release) {
    const u32 base = domain == 1 ? 0x04600014U : 0x04600024U;
    bus.write(base, 4, latency);
    bus.write(base + 4, 4, pulse);
    bus.write(base + 8, 4, page);
    bus.write(base + 12, 4, release);
}

void start_dma(Bus& bus, u32 address, u32 length, bool to_dram = true) {
    bus.write(0x04600000, 4, 0x2000);
    bus.write(0x04600004, 4, address);
    bus.write(to_dram ? 0x0460000cU : 0x04600008U, 4, length);
}

void check_deadline(Bus& bus, u64 cycles) {
    CHECK_EQ(bus.read(0x04600010, 4), 1U);
    bus.tick(cycles - 1);
    CHECK_EQ(bus.read(0x04600010, 4), 1U);
    CHECK_EQ(bus.read(0x04300008, 4) & 0x10U, 0U);
    bus.tick(1);
    CHECK_EQ(bus.read(0x04600010, 4), 8U);
    CHECK_EQ(bus.read(0x04300008, 4) & 0x10U, 0x10U);
}
} // namespace

TEST(pi_dma_completion_uses_programmed_bus_timing) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.rom = {0x12, 0x34, 0x56, 0x78, 0x23, 0x45, 0x67, 0x89};
    configure_domain(bus, 1, 3, 5, 5, 1);
    start_dma(bus, 0x10000000, 7);
    CHECK_EQ(bus.read(0x2000, 4), 0U);
    CHECK_EQ(bus.read(0x2004, 4), 0U);
    bus.tick(57);
    CHECK_EQ(bus.read(0x2000, 4), 0U);
    CHECK_EQ(bus.read(0x2004, 4), 0U);
    CHECK_EQ(bus.read(0x04600010, 4), 1U);
    bus.tick(1);
    CHECK_EQ(bus.read(0x2000, 4), 0x12345678U);
    CHECK_EQ(bus.read(0x2004, 4), 0x23456789U);
    CHECK_EQ(bus.read(0x04600010, 4), 8U);
    CHECK_EQ(bus.read(0x04300008, 4) & 0x10U, 0x10U);
}

TEST(pi_dma_timing_fields_independently_delay_completion) {
    for (unsigned field = 0; field < 3; ++field) {
        System system;
        configure_domain(system.bus, 1, field == 0 ? 4U : 3U, field == 1 ? 6U : 5U, 5, field == 2 ? 2U : 1U);
        start_dma(system.bus, 0x10000000, 7);
        check_deadline(system.bus, field == 0 ? 59U : 62U);
    }
}

TEST(pi_dma_uses_the_starting_address_domain) {
    for (u32 address : {0x05000000U, 0x08000000U, 0x0f000000U, 0x06000000U, 0x10000000U}) {
        for (bool to_dram : {false, true}) {
            System system;
            configure_domain(system.bus, 1, 3, 5, 5, 1);
            configure_domain(system.bus, 2, 7, 9, 5, 2);
            start_dma(system.bus, address, 7, to_dram);
            const bool domain2 = address == 0x05000000U || address == 0x08000000U || address == 0x0f000000U;
            check_deadline(system.bus, domain2 ? 82U : 58U);
        }
    }
}

TEST(pi_dma_accounts_for_page_crossings_and_buffer_overhead) {
    struct Case {
        u32 offset;
        u32 length;
        u32 page;
        u64 cycles;
    };
    for (const auto& sample :
         {Case{0, 127, 5, 558}, Case{124, 7, 5, 76}, Case{0, 255, 5, 1116}, Case{4, 255, 5, 1234},
          Case{0, 383, 5, 1674}, Case{0, 7, 0, 124}, Case{0, 7, 1, 58}, Case{0, 7, 15, 58}}) {
        System system;
        configure_domain(system.bus, 1, 3, 5, sample.page, 1);
        start_dma(system.bus, 0x10000000 + sample.offset, sample.length);
        check_deadline(system.bus, sample.cycles);
    }
}

TEST(pi_dma_rounds_bus_length_to_complete_halfwords) {
    for (bool to_dram : {false, true}) {
        for (u32 length : {0U, 1U, 6U, 7U}) {
            System system;
            configure_domain(system.bus, 1, 3, 5, 5, 1);
            start_dma(system.bus, 0x10000000, length, to_dram);
            check_deadline(system.bus, length < 2 ? 28U : 58U);
        }
    }
}

TEST(pi_dma_completion_uses_rcp_clock_units) {
    for (bool split : {false, true}) {
        System system;
        configure_domain(system.bus, 1, 3, 5, 5, 1);
        start_dma(system.bus, 0x10000000, 7);
        if (split) {
            for (unsigned cycle = 0; cycle < 86; ++cycle)
                system.advance(1);
        } else {
            system.advance(86);
        }
        CHECK_EQ(system.bus.read(0x04600010, 4), 1U);
        system.advance(1);
        CHECK_EQ(system.bus.read(0x04600010, 4), 8U);
    }
}

TEST(pi_dma_reset_cancels_the_completion_interrupt) {
    System system;
    configure_domain(system.bus, 1, 3, 5, 5, 1);
    start_dma(system.bus, 0x10000000, 7);
    system.bus.tick(10);
    system.bus.write(0x04600010, 4, 1);
    system.bus.tick(1000);
    CHECK_EQ(system.bus.read(0x04600010, 4), 0U);
    CHECK_EQ(system.bus.read(0x04300008, 4) & 0x10U, 0U);
}

TEST(pi_dma_cart_to_dram_exposes_each_internal_buffer_at_its_deadline) {
    for (bool split : {false, true}) {
        System system;
        test::initialize_memory(system);
        auto& bus = system.bus;
        bus.rom.resize(256);
        for (u32 index = 0; index < bus.rom.size(); ++index)
            bus.rom[index] = static_cast<u8>(index);
        configure_domain(bus, 1, 0, 0, 8, 0); // One 1024-byte PI page, two 128-byte DMA buffers.
        start_dma(bus, 0x10000000, 255);

        const auto advance = [&](u64 cycles) {
            if (split) {
                for (u64 cycle = 0; cycle < cycles; ++cycle)
                    bus.tick(1);
            } else {
                bus.tick(cycles);
            }
        };

        advance(270);
        CHECK_EQ(bus.read_ram_byte(0x2000), 0U);
        CHECK_EQ(bus.read_ram_byte(0x207f), 0U);
        CHECK_EQ(bus.read_ram_byte(0x2080), 0U);
        CHECK_EQ(bus.read(0x04600000, 4), 0x2000U);
        CHECK_EQ(bus.read(0x04600004, 4), 0x10000000U);

        advance(1);
        for (u32 index = 0; index < 128; ++index)
            CHECK_EQ(bus.read_ram_byte(0x2000 + index), static_cast<u8>(index));
        CHECK_EQ(bus.read_ram_byte(0x2080), 0U);
        CHECK_EQ(bus.read(0x04600000, 4), 0x2080U);
        CHECK_EQ(bus.read(0x04600004, 4), 0x10000080U);
        CHECK_EQ(bus.read(0x04600010, 4), 1U);

        advance(255);
        CHECK_EQ(bus.read_ram_byte(0x2080), 0U);
        CHECK_EQ(bus.read(0x04600010, 4), 1U);
        advance(1);
        for (u32 index = 0; index < 256; ++index)
            CHECK_EQ(bus.read_ram_byte(0x2000 + index), static_cast<u8>(index));
        CHECK_EQ(bus.read(0x04600000, 4), 0x2100U);
        CHECK_EQ(bus.read(0x04600004, 4), 0x10000100U);
        CHECK_EQ(bus.read(0x04600010, 4), 8U);
    }
}

TEST(pi_dma_rdram_to_cart_samples_each_page_when_its_transfer_retires) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.set_save_type(SaveType::Sram);
    configure_domain(bus, 2, 0, 0, 1, 0); // Two 8-byte pages complete at cycles 51 and 102.
    for (u32 index = 0; index < 8; ++index) {
        bus.write_ram_byte(0x2000 + index, static_cast<u8>(0x10 + index));
        bus.write_ram_byte(0x2008 + index, static_cast<u8>(0x20 + index));
    }
    start_dma(bus, 0x08000000, 15, false);

    bus.tick(50);
    for (u32 index = 0; index < 16; ++index)
        CHECK_EQ(bus.sram[index], 0U);
    bus.tick(1);
    for (u32 index = 0; index < 8; ++index) {
        CHECK_EQ(bus.sram[index], static_cast<u8>(0x10 + index));
        CHECK_EQ(bus.sram[8 + index], 0U);
        bus.write_ram_byte(0x2000 + index, static_cast<u8>(0x30 + index));
        bus.write_ram_byte(0x2008 + index, static_cast<u8>(0x40 + index));
    }

    bus.tick(50);
    for (u32 index = 0; index < 8; ++index)
        CHECK_EQ(bus.sram[8 + index], 0U);
    bus.tick(1);
    for (u32 index = 0; index < 8; ++index) {
        CHECK_EQ(bus.sram[index], static_cast<u8>(0x10 + index));
        CHECK_EQ(bus.sram[8 + index], static_cast<u8>(0x40 + index));
    }
    CHECK_EQ(bus.read(0x04600010, 4), 8U);
}

TEST(pi_dma_abort_keeps_only_blocks_that_reached_their_progress_deadline) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.rom.resize(256, 0x5a);
    configure_domain(bus, 1, 0, 0, 8, 0);
    start_dma(bus, 0x10000000, 255);
    bus.tick(271);
    for (u32 index = 0; index < 128; ++index)
        CHECK_EQ(bus.read_ram_byte(0x2000 + index), 0x5aU);
    for (u32 index = 128; index < 256; ++index)
        CHECK_EQ(bus.read_ram_byte(0x2000 + index), 0U);

    bus.write(0x04600010, 4, 1);
    CHECK_EQ(bus.read(0x04600010, 4), 0U);
    bus.tick(1000);
    for (u32 index = 0; index < 128; ++index)
        CHECK_EQ(bus.read_ram_byte(0x2000 + index), 0x5aU);
    for (u32 index = 128; index < 256; ++index)
        CHECK_EQ(bus.read_ram_byte(0x2000 + index), 0U);
    CHECK_EQ(bus.read(0x04300008, 4) & 0x10U, 0U);
}

TEST(pi_dma_busy_register_writes_set_error_without_retargeting_the_transfer) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.rom.assign(8, 0xa5);
    configure_domain(bus, 1, 3, 5, 5, 1);
    start_dma(bus, 0x10000000, 7);
    bus.write(0x04600000, 4, 0x3000);
    bus.write(0x04600004, 4, 0x10000040);
    bus.write(0x0460001c, 4, 0);
    CHECK_EQ(bus.read(0x04600010, 4), 5U);
    CHECK_EQ(bus.read(0x04600000, 4), 0x2000U);
    CHECK_EQ(bus.read(0x04600004, 4), 0x10000000U);
    CHECK_EQ(bus.read(0x0460001c, 4), 5U);

    bus.tick(58);
    CHECK_EQ(bus.read(0x2000, 4), 0xa5a5a5a5U);
    CHECK_EQ(bus.read(0x04600010, 4), 12U);
    bus.write(0x04600010, 4, 1);
    CHECK_EQ(bus.read(0x04600010, 4), 8U);
}

TEST(pi_dma_cpu_cartridge_io_uses_the_bus_without_retargeting_dma_progress) {
    System system;
    test::initialize_memory(system);
    auto& bus = system.bus;
    bus.rom.resize(256);
    for (u32 index = 0; index < bus.rom.size(); ++index)
        bus.rom[index] = static_cast<u8>(index);
    bus.set_save_type(SaveType::Sram);
    configure_domain(bus, 1, 0, 0, 8, 0);
    start_dma(bus, 0x10000000, 255);

    CHECK_EQ(bus.read(0x10000080, 4), 0x80818283U);
    CHECK_EQ(bus.read(0x04600010, 4), 1U);
    CHECK_EQ(bus.read(0x04600004, 4), 0x10000084U);
    CHECK_EQ(bus.read(0x04600034, 4), 0x80818283U);

    bus.write(0x08000000, 4, 0x12345678);
    CHECK_EQ(bus.read(0x04600010, 4), 3U);
    CHECK_EQ(bus.read(0x04600004, 4), 0x08000004U);
    CHECK_EQ(bus.read(0x04600034, 4), 0x12345678U);
    CHECK_EQ(read_be32(bus.sram.data()), 0x12345678U);

    bus.tick(139);
    CHECK_EQ(bus.read(0x04600010, 4), 3U);
    bus.tick(1);
    CHECK_EQ(bus.read(0x04600010, 4), 1U);
    bus.tick(130);
    CHECK_EQ(bus.read_ram_byte(0x2000), 0U);
    bus.tick(1);
    for (u32 index = 0; index < 128; ++index)
        CHECK_EQ(bus.read_ram_byte(0x2000 + index), static_cast<u8>(index));
    CHECK_EQ(bus.read(0x04600004, 4), 0x10000080U);
    CHECK_EQ(bus.read(0x04600034, 4), 0x7e7f7e7fU);
    CHECK_EQ(bus.read(0x04600010, 4), 1U);

    bus.tick(256);
    for (u32 index = 0; index < 256; ++index)
        CHECK_EQ(bus.read_ram_byte(0x2000 + index), static_cast<u8>(index));
    CHECK_EQ(bus.read(0x04600010, 4), 8U);
}

TEST(pi_domain_timing_registers_mask_unused_bits) {
    System system;
    for (unsigned domain : {1U, 2U}) {
        configure_domain(system.bus, domain, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff);
        const u32 base = domain == 1 ? 0x04600014U : 0x04600024U;
        CHECK_EQ(system.bus.read(base, 4), 0xffU);
        CHECK_EQ(system.bus.read(base + 4, 4), 0xffU);
        CHECK_EQ(system.bus.read(base + 8, 4), 0xfU);
        CHECK_EQ(system.bus.read(base + 12, 4), 3U);
    }
}
