#include "cupid/system.hpp"
#include "rcp/joybus_transport.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>

namespace {
using namespace cupid;

constexpr u64 second = CartridgeRtc::cycles_per_second;
constexpr std::array<u8, 8> initial_time{0x58, 0x59, 0xa3, 0x31, 0x05, 0x12, 0x99, 0};

u8 bcd(unsigned value) {
    return static_cast<u8>(value / 10 * 16 + value % 10);
}

CartridgeRtc::Registers initial_registers() {
    CartridgeRtc::Registers registers{};
    registers[0] = 3;
    std::copy(initial_time.begin(), initial_time.end(), registers.begin() + 16);
    return registers;
}

struct RtcFixture {
    System system;
    Bus& bus{system.bus};
    u32 output_offset{};

    RtcFixture() {
        test::initialize_memory(system);
        bus.write(0x04700010, 4, 0);
        bus.rtc.emplace(initial_registers());
    }

    void packet(std::span<const u8> input, u8 receive, unsigned channel = 4) {
        CHECK(channel <= 4);
        CHECK(channel + 2 + input.size() + receive < 63);
        std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
        bus.pif[0x7c0 + channel] = static_cast<u8>(input.size());
        bus.pif[0x7c1 + channel] = receive;
        std::copy(input.begin(), input.end(), bus.pif.begin() + 0x7c2 + channel);
        output_offset = 0x7c2 + channel + static_cast<u32>(input.size());
        std::fill_n(bus.pif.begin() + output_offset, receive, u8{0xcc});
        bus.pif[output_offset + receive] = 0xfe;
    }

    std::vector<u8> execute(u8 receive) {
        bus.joybus.configure();
        bus.joybus.execute();
        CHECK_EQ(bus.pif[output_offset + receive], 0xfeU);
        return {bus.pif.begin() + output_offset, bus.pif.begin() + output_offset + receive};
    }

    std::vector<u8> command(std::initializer_list<u8> input, u8 receive) {
        packet({input.begin(), input.size()}, receive);
        return execute(receive);
    }

    u8 write(u8 block, const std::array<u8, 8>& bytes) {
        std::array<u8, 10> input{8, block};
        std::copy(bytes.begin(), bytes.end(), input.begin() + 2);
        packet(input, 1);
        return execute(1)[0];
    }

    u8 control(u16 value) {
        return write(0, {static_cast<u8>(value >> 8), static_cast<u8>(value), 0, 0, 0x12, 0x34, 0x56, 0x78});
    }

    std::array<u8, 8> read(u8 block) {
        auto result = command({7, block}, 9);
        CHECK_EQ(bus.pif[0x7c5], 9U);
        std::array<u8, 8> bytes{};
        std::copy_n(result.begin(), 8, bytes.begin());
        return bytes;
    }

    void start_dma(bool to_ram) {
        test::configure_joybus(bus, to_ram);
        bus.write(0x04800000, 4, 0x2000);
        bus.write(0x04800004, 4, 0x1fc007c0);
    }
};
} // namespace

TEST(rtc_presence_is_independent_of_every_save_chip_and_channel) {
    for (auto type :
         {SaveType::None, SaveType::Sram, SaveType::FlashRam, SaveType::Eeprom4K, SaveType::Eeprom16K}) {
        RtcFixture fixture;
        fixture.bus.set_save_type(type);
        CHECK_EQ(fixture.command({6}, 3), (std::vector<u8>{0, 0x10, 0}));
        CHECK_EQ(fixture.bus.pif[0x7c5], 3U);
        for (unsigned channel = 0; channel < 4; ++channel) {
            const std::array<u8, 1> input{6};
            fixture.packet(input, 3, channel);
            CHECK_EQ(fixture.execute(3), (std::vector<u8>{0xcc, 0xcc, 0xcc}));
            CHECK_EQ(fixture.bus.pif[0x7c1 + channel], 0x83U);
        }
        fixture.bus.rtc.reset();
        for (const auto& input :
             {std::vector<u8>{6}, std::vector<u8>{7, 2}, std::vector<u8>{8, 0, 0, 4, 0, 0, 0, 0, 0, 0}}) {
            fixture.packet(input, 9);
            CHECK_EQ(fixture.execute(9), std::vector<u8>(9, 0xcc));
            CHECK_EQ(fixture.bus.pif[0x7c5], 0x89U);
        }
    }
}

TEST(rtc_stop_set_run_sequence_preserves_calibration_and_reports_status) {
    RtcFixture fixture;
    CHECK_EQ(fixture.control(4), 0x80U);
    CHECK_EQ(fixture.command({6}, 3), (std::vector<u8>{0, 0x10, 0x80}));
    const std::array<u8, 8> time{3, 2, 0x81, 3, 1, 0x11, 3, 1};
    CHECK_EQ(fixture.write(2, time), 0x80U);
    fixture.bus.tick(3 * second);
    CHECK_EQ(fixture.read(2), time);
    CHECK_EQ(fixture.control(0x0300), 0U);
    CHECK_EQ(fixture.read(0), (std::array<u8, 8>{3, 0, 0, 0, 0x12, 0x34, 0x56, 0x78}));
    fixture.bus.tick(second - 1);
    CHECK_EQ(fixture.read(2), time);
    fixture.bus.tick(1);
    CHECK_EQ(fixture.read(2)[0], 4U);
    CHECK_EQ(fixture.command({6}, 3)[2], 0U);
}

TEST(rtc_block_locks_are_independent_and_do_not_block_clock_advancement) {
    RtcFixture fixture;
    const std::array<u8, 8> bytes{0x11, 0x22, 0x83, 4, 5, 6, 7, 1};
    for (u16 control : {u16{0}, u16{0x100}, u16{0x200}, u16{0x300}}) {
        fixture.bus.rtc.emplace(initial_registers());
        CHECK_EQ(fixture.control(control), 0U);
        for (u8 block : {u8{1}, u8{2}}) {
            auto before = fixture.read(block);
            CHECK_EQ(fixture.write(block, bytes), 0U);
            const bool locked = (control & (block == 1 ? 0x100U : 0x200U)) != 0;
            CHECK_EQ(fixture.read(block), locked ? before : bytes);
        }
        const auto before = fixture.read(2);
        fixture.bus.tick(second);
        CHECK_EQ(fixture.read(2)[0], static_cast<u8>(before[0] + 1));
    }
}

TEST(rtc_block_addresses_decode_two_bits_and_keep_banks_separate) {
    RtcFixture fixture;
    fixture.control(4);
    for (unsigned address = 0; address < 256; ++address) {
        const u8 block = static_cast<u8>(address);
        std::array<u8, 8> bytes{};
        bytes.fill(block);
        if ((block & 3U) == 0) {
            bytes[0] = 0;
            bytes[1] = 4;
        }
        const auto before = fixture.bus.rtc->registers();
        CHECK_EQ(fixture.write(block, bytes), 0x80U);
        CHECK_EQ(fixture.read(block), bytes);
        for (unsigned index = 0; index < 32; ++index)
            CHECK_EQ(fixture.bus.rtc->registers()[index],
                     index / 8 == (block & 3U) ? bytes[index % 8] : before[index]);
    }
}

TEST(rtc_rejects_short_packets_without_changing_registers_or_replies) {
    RtcFixture fixture;
    const auto original = fixture.bus.rtc->registers();
    for (u8 command : {u8{6}, u8{7}, u8{8}, u8{9}})
        for (u8 send = 1; send <= 11; ++send)
            for (u8 receive = 0; receive <= 10; ++receive) {
                const bool valid = (command == 6 && receive >= 3) ||
                                   (command == 7 && send >= 2 && receive >= 9) ||
                                   (command == 8 && send >= 10 && receive >= 1);
                if (valid)
                    continue;
                std::vector<u8> input(send, 0);
                input[0] = command;
                fixture.packet(input, receive);
                CHECK_EQ(fixture.execute(receive), std::vector<u8>(receive, 0xcc));
                CHECK_EQ(fixture.bus.pif[0x7c5], receive | 0x80U);
                CHECK_EQ(fixture.bus.rtc->registers(), original);
            }
}

TEST(rtc_ignores_extra_command_bytes_and_zero_pads_replies) {
    RtcFixture fixture;
    CHECK_EQ(fixture.command({6, 0xaa}, 5), (std::vector<u8>{0, 0x10, 0, 0, 0}));
    CHECK_EQ(fixture.command({8, 0, 0, 4, 1, 2, 3, 4, 5, 6, 0xee}, 3), (std::vector<u8>{0x80, 0, 0}));
    CHECK_EQ(fixture.command({7, 0, 0xaa}, 11), (std::vector<u8>{0, 4, 1, 2, 3, 4, 5, 6, 0x80, 0, 0}));
}

TEST(rtc_calendar_carries_match_month_ends_from_1996_through_2099) {
    using namespace std::chrono;
    for (int yr = 1996; yr <= 2099; ++yr)
        for (unsigned mo = 1; mo <= 12; ++mo) {
            const auto last_date = year{yr} / month{mo} / last;
            const sys_days date{last_date};
            const year_month_day next{date + days{1}};
            const unsigned weekday_value = weekday{date}.c_encoding();
            auto registers = initial_registers();
            const std::array<u8, 8> time{0x59,
                                         0x59,
                                         0xa3,
                                         bcd(static_cast<unsigned>(last_date.day())),
                                         bcd(weekday_value),
                                         bcd(mo),
                                         bcd(static_cast<unsigned>(yr - 1900) % 100),
                                         bcd(static_cast<unsigned>(yr - 1900) / 100)};
            std::copy(time.begin(), time.end(), registers.begin() + 16);
            CartridgeRtc rtc(registers);
            rtc.tick(second - 1);
            CHECK_EQ(rtc.registers(), registers);
            rtc.tick(1);
            const unsigned next_year = static_cast<unsigned>(static_cast<int>(next.year()) - 1900);
            const std::array<u8, 8> expected{0,
                                             0,
                                             0x80,
                                             bcd(static_cast<unsigned>(next.day())),
                                             bcd((weekday_value + 1) % 7),
                                             bcd(static_cast<unsigned>(next.month())),
                                             bcd(next_year % 100),
                                             bcd(next_year / 100)};
            CHECK(std::equal(expected.begin(), expected.end(), rtc.registers().begin() + 16));
        }
}

TEST(rtc_bulk_and_fragmented_ticks_preserve_fractional_seconds) {
    CartridgeRtc bulk(initial_registers());
    CartridgeRtc fragmented(initial_registers());
    bulk.tick(3662 * second + 123);
    fragmented.tick(second - 1);
    fragmented.tick(2);
    fragmented.tick(123);
    fragmented.tick(3661 * second - 1);
    CHECK_EQ(bulk.registers(), fragmented.registers());
    CHECK_EQ(bulk.next_tick(), second - 123);
    CHECK_EQ(bulk.next_tick(), fragmented.next_tick());
    const std::array<u8, 8> expected{0, 1, 0x81, 1, 6, 1, 0, 1};
    CHECK(std::equal(expected.begin(), expected.end(), bulk.registers().begin() + 16));
}

TEST(rtc_console_reset_preserves_registers_and_locks_and_restarts_fraction) {
    RtcFixture fixture;
    fixture.bus.tick(second - 1);
    fixture.system.reset();
    CHECK_EQ(fixture.read(2), initial_time);
    CHECK_EQ(fixture.write(2, {}), 0U);
    CHECK_EQ(fixture.read(2), initial_time);
    fixture.bus.tick(second - 1);
    CHECK_EQ(fixture.read(2), initial_time);
    fixture.bus.tick(1);
    CHECK_EQ(fixture.read(2)[0], 0x59U);
    fixture.control(4);
    fixture.system.reset();
    fixture.bus.tick(2 * second);
    CHECK_EQ(fixture.command({6}, 3)[2], 0x80U);
    CHECK_EQ(fixture.read(2)[0], 0x59U);
}

TEST(rtc_eeprom_busy_state_and_commands_are_independent) {
    RtcFixture fixture;
    fixture.bus.set_save_type(SaveType::Eeprom4K);
    CHECK_EQ(fixture.command({5, 1, 0x42}, 1)[0], 0U);
    CHECK_EQ(fixture.command({0}, 3)[2], 0x80U);
    CHECK_EQ(fixture.command({6}, 3)[2], 0U);
    fixture.control(4);
    CHECK_EQ(fixture.command({0xff}, 3)[2], 0x80U);
    fixture.bus.tick(375000);
    CHECK_EQ(fixture.command({0}, 3)[2], 0U);
    CHECK_EQ(fixture.command({6}, 3)[2], 0x80U);
    CHECK_EQ(fixture.command({4, 1}, 1)[0], 0x42U);
    CHECK_EQ(fixture.read(2), initial_time);
}

TEST(rtc_control_writes_restart_the_fraction_but_time_and_locked_writes_do_not) {
    RtcFixture fixture;
    fixture.bus.tick(second - 1);
    fixture.control(0x300);
    fixture.bus.tick(second - 1);
    CHECK_EQ(fixture.read(2), initial_time);
    fixture.write(2, {});
    fixture.bus.tick(1);
    CHECK_EQ(fixture.read(2)[0], 0x59U);
    fixture.control(0);
    fixture.bus.tick(second - 1);
    fixture.write(2, initial_time);
    fixture.bus.tick(1);
    CHECK_EQ(fixture.read(2)[0], 0x59U);
}

TEST(rtc_cpu_to_rcp_conversion_preserves_the_one_second_boundary) {
    for (bool fragmented : {false, true}) {
        RtcFixture fixture;
        if (fragmented) {
            fixture.system.advance(1);
            fixture.system.advance(93'749'998);
        } else {
            fixture.system.advance(93'749'999);
        }
        CHECK_EQ(fixture.read(2), initial_time);
        fixture.system.advance(1);
        CHECK_EQ(fixture.read(2)[0], 0x59U);
        fixture.system.advance(93'750'000);
        CHECK_EQ(fixture.read(2), (std::array<u8, 8>{0, 0, 0x80, 1, 6, 1, 0, 1}));
    }
}

TEST(rtc_si_completion_sees_the_tick_at_the_same_boundary) {
    for (bool to_ram : {false, true})
        for (bool cpu : {false, true})
            for (bool single : {false, true}) {
                RtcFixture fixture;
                const u64 deadline = 39280;
                fixture.bus.tick(second - deadline - (to_ram ? 0 : 4065));
                const std::array<u8, 2> input{7, 2};
                fixture.packet(input, 9);
                fixture.start_dma(to_ram);
                const auto advance = [&](u64 cycles) {
                    if (single) {
                        for (u64 count = 0; count < cycles; ++count)
                            cpu ? fixture.system.advance(1) : fixture.bus.tick(1);
                    } else {
                        cpu ? fixture.system.advance(cycles) : fixture.bus.tick(cycles);
                    }
                };
                const u64 cycles = cpu ? (deadline * 3 + 1) / 2 : deadline;
                advance(cycles - 1);
                CHECK_EQ(fixture.bus.rtc->registers()[16], 0x58U);
                CHECK((fixture.bus.read(0x04800018, 4) & 1U) != 0);
                advance(1);
                CHECK_EQ(fixture.bus.rtc->registers()[16], 0x59U);
                CHECK_EQ(fixture.bus.read(0x04800018, 4) & 1U, 0U);
                CHECK_EQ(to_ram ? fixture.bus.read_ram_byte(0x2008) : fixture.bus.pif[0x7c8], 0x59U);
            }
}

TEST(rtc_si_run_command_starts_a_full_second_at_completion) {
    for (bool to_ram : {false, true}) {
        RtcFixture fixture;
        fixture.control(4);
        fixture.bus.tick(second - 1);
        const std::array<u8, 10> input{8, 0, 3, 0};
        fixture.packet(input, 1);
        fixture.start_dma(to_ram);
        const u64 deadline = 39280;
        fixture.bus.tick(deadline - 1);
        CHECK(!fixture.bus.rtc->running());
        fixture.bus.tick(1);
        CHECK(fixture.bus.rtc->running());
        fixture.bus.tick(second - 1);
        CHECK_EQ(fixture.read(2), initial_time);
        fixture.bus.tick(1);
        CHECK_EQ(fixture.read(2)[0], 0x59U);
    }
}
