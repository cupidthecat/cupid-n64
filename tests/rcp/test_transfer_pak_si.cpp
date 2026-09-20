#include "joybus_transport.hpp"
#include "test_system.hpp"
#include "transfer_pak_fixture.hpp"

namespace {
using namespace cupid;
using test::address_remainder;
using test::data_remainder;

struct TransferDma : test::TransferFixture {
    unsigned port;
    bool read_dma, cpu_clock, single;
    u64 fraction{};

    TransferDma(unsigned target, bool read, bool cpu, bool one)
        : port(target), read_dma(read), cpu_clock(cpu), single(one) {
        test::initialize_memory(system);
    }

    u64 deadline() const {
        return 37020 + port * 1420 + (read_dma ? 0 : 4065);
    }

    std::vector<u8> dma(std::span<const u8> input, u8 receive, const std::function<void()>& before = {}) {
        packet(port, input, receive);
        test::configure_joybus(bus, read_dma);
        bus.write(0x04800000, 4, 0x2000);
        bus.write(0x04800004, 4, 0x1fc007c0);
        const u64 rcp = 37020 + port * 1420;
        const u64 cycles = cpu_clock ? (rcp * 3 - fraction + 1) / 2 : rcp;
        if (cpu_clock)
            fraction = (fraction + cycles * 2) % 3;
        const auto advance = [&](u64 amount) {
            if (cpu_clock)
                system.advance(amount);
            else
                bus.tick(amount);
        };
        if (single) {
            for (u64 cycle = 0; cycle < cycles - 1; ++cycle)
                advance(1);
        } else {
            advance(cycles - 1);
        }
        CHECK_EQ(bus.read(0x04800018, 4) & 1U, 1U);
        CHECK_EQ(bus.pif[response], 0xccU);
        if (before)
            before();
        advance(1);
        CHECK_EQ(bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
        CHECK_EQ(bus.pif[0x7c1 + port], receive);
        if (read_dma)
            for (u32 index = 0; index < 64; ++index)
                CHECK_EQ(bus.read_ram_byte(0x2000 + index), bus.pif[0x7c0 + index]);
        bus.write(0x04800018, 4, 0);
        return {bus.pif.begin() + response, bus.pif.begin() + response + receive};
    }

    void write_dma(u16 address, u8 value, const std::function<void()>& before = {}) {
        const auto encoded = static_cast<u16>(address | address_remainder(address));
        std::array<u8, 35> input{3, static_cast<u8>(encoded >> 8U), static_cast<u8>(encoded)};
        std::fill(input.begin() + 3, input.end(), value);
        CHECK_EQ(dma(input, 1, before)[0], data_remainder(std::span<const u8, 32>(input.data() + 3, 32)));
    }

    std::vector<u8> read_dma_block(u16 address) {
        const auto encoded = static_cast<u16>(address | address_remainder(address));
        const std::array<u8, 3> input{2, static_cast<u8>(encoded >> 8U), static_cast<u8>(encoded)};
        const auto reply = dma(input, 33);
        CHECK_EQ(reply[32], data_remainder(std::span<const u8, 32>(reply.data(), 32)));
        return reply;
    }
};
} // namespace

TEST(transfer_pak_si_dma_initialization_banking_and_save_readback_obey_completion_boundaries) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool read : {false, true})
            for (bool cpu : {false, true})
                for (bool single : {false, true}) {
                    TransferDma f(port, read, cpu, single);
                    f.bus.transfer_paks[port].insert(test::game_boy(GameBoyMapper::Mbc1, 8, 0x2000));
                    auto& pak = f.bus.transfer_paks[port];
                    const std::array<u8, 1> status{0};
                    CHECK_EQ(f.dma(status, 3)[2], 3U);
                    f.write_dma(0x8000, 0x84, [&] { CHECK_EQ(pak.read(0x8000), 0U); });
                    CHECK_EQ(f.read_dma_block(0x8000)[0], 0x84U);
                    f.write_dma(0xb000, 1);
                    f.write_dma(0xa000, 0, [&] { CHECK_EQ(pak.read(0xa000), 3U); });
                    f.write_dma(0xe000, 5, [&] { CHECK_EQ(test::game_boy_bank(*pak.cartridge()), 1U); });
                    CHECK_EQ(test::game_boy_bank(*pak.cartridge()), 5U);
                    f.write_dma(0xa000, 1);
                    const auto rom = f.read_dma_block(0xc000);
                    for (unsigned i = 0; i < 32; ++i)
                        CHECK_EQ(rom[i], i == 1 ? 0U : 5U);
                    f.write_dma(0xa000, 0);
                    f.write_dma(0xc000, 10);
                    f.write_dma(0xa000, 2);
                    f.write_dma(0xe000, 0x6d, [&] { CHECK_EQ(pak.cartridge()->ram()[0], 0U); });
                    const auto ram = f.read_dma_block(0xe000);
                    for (unsigned i = 0; i < 32; ++i)
                        CHECK_EQ(ram[i], 0x6dU);
                }
}

TEST(transfer_pak_clock_tick_precedes_a_latch_at_the_same_si_completion) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool read : {false, true})
            for (bool cpu : {false, true})
                for (bool single : {false, true}) {
                    TransferDma f(port, read, cpu, single);
                    f.bus.transfer_paks[port].insert(test::game_boy(GameBoyMapper::Mbc3, 2, 0, true));
                    f.start(port);
                    f.write_block(port, 0xa000, 1);
                    auto& cart = *f.bus.transfer_paks[port].cartridge();
                    cart.write(0, 10);
                    cart.write(0x4000, 8);
                    cart.set_clock({});
                    cart.tick(GameBoyCartridge::cycles_per_second - f.deadline());
                    f.write_dma(0xe000, 1, [&] {
                        CHECK_EQ(cart.clock().seconds, 0U);
                        CHECK_EQ(cart.read(0xa000), 0U);
                    });
                    CHECK_EQ(cart.clock().seconds, 1U);
                    CHECK_EQ(cart.read(0xa000), 1U);
                }
}

TEST(transfer_pak_malformed_commands_and_pending_detection_do_not_change_registers) {
    test::TransferFixture f;
    for (unsigned port = 0; port < 4; ++port) {
        std::array<u8, 32> data{};
        data.fill(0x84);
        CHECK_EQ(f.transfer(port, 0x8000, data)[0], data_remainder(data) ^ 0xffU);
        CHECK_EQ(f.bus.transfer_paks[port].read(0x8000), 0U);
        f.start(port);
        for (unsigned command = 4; command < 255; ++command) {
            const auto reply = f.command(port, {static_cast<u8>(command)}, 3);
            CHECK(reply == std::vector<u8>(3, 0xcc));
            CHECK_EQ(f.bus.pif[0x7c1 + port], 0x83U);
        }
        for (const auto& input :
             {std::vector<u8>{2}, std::vector<u8>{2, 0x80}, std::vector<u8>{3}, std::vector<u8>{3, 0x80},
              std::vector<u8>{3, 0x80, address_remainder(0x8000)}}) {
            f.packet(port, input, 3);
            CHECK(f.execute(3) == std::vector<u8>(3, 0xcc));
            CHECK_EQ(f.bus.pif[0x7c1 + port], 0x83U);
        }
        f.command(port, {3, 0x80, address_remainder(0x8000), 0xfe}, 0);
        CHECK_EQ(f.bus.pif[0x7c1 + port], 0x80U);
        CHECK_EQ(f.transfer(port, 0x8000)[0], 0x84U);
    }
}
