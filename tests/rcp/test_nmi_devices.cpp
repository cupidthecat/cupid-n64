#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>

namespace {
using namespace cupid;
constexpr u64 code = 0xffffffff80001000ULL;

void prepare(System& system) {
    test::initialize_memory(system);
    system.bus.write(0x04700010, 4, 0);
    system.cpu.cp0[12] = 0x10000000;
    system.cpu.set_pc(code);
}

u8 eeprom_status(Bus& bus) {
    std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
    bus.pif[0x7c4] = 1;
    bus.pif[0x7c5] = 3;
    bus.pif[0x7ca] = 0xfe;
    bus.joybus.configure();
    bus.joybus.execute();
    CHECK_EQ(bus.pif[0x7ca], 0xfeU);
    return bus.pif[0x7c9];
}
} // namespace

TEST(cpu_nmi_keeps_older_buffered_stores_and_their_bus_deadlines) {
    for (unsigned width : {1U, 2U, 4U, 8U}) {
        System system;
        prepare(system);
        auto& cpu = system.cpu;
        const u32 opcode = width == 1 ? 0x28 : width == 2 ? 0x29 : width == 4 ? 0x2b : 0x3f;
        system.bus.write(0x1000, 4, (opcode << 26U) | (4U << 21U) | (2U << 16U));
        cpu.gpr[4] = 0xffffffffa0002000ULL;
        cpu.gpr[2] = 0x123456789abcdef0ULL;
        u64 ignored = 0;
        CHECK(cpu.read_memory(code, 4, ignored, true));
        cpu.step();
        CHECK_EQ(system.bus.read(0x2000, width), 0U);
        cpu.request_nmi();
        cpu.step();
        CHECK_EQ(cpu.cp0[30], code + 4);
        CHECK_EQ(system.bus.read(0x2000, width), 0U);
        const u64 remaining_cpu = width == 8 ? 3 : 1;
        system.advance(remaining_cpu - 1);
        CHECK_EQ(system.bus.read(0x2000, width), 0U);
        system.advance(1);
        const u64 mask = width == 8 ? ~0ULL : (1ULL << (width * 8)) - 1;
        CHECK_EQ(system.bus.read(0x2000, width), cpu.gpr[2] & mask);
    }
}

TEST(cpu_nmi_preserves_inflight_si_transfers_descriptors_and_device_interrupts) {
    for (bool cpu_clock : {false, true})
        for (bool single : {false, true}) {
            System system;
            prepare(system);
            auto& bus = system.bus;
            ControllerState state;
            state.buttons = 0x8000;
            bus.set_controller_state(0, state);
            std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
            bus.pif[0x7c0] = 1;
            bus.pif[0x7c1] = 4;
            bus.pif[0x7c2] = 1;
            bus.pif[0x7c7] = 0xfe;
            bus.joybus.configure();
            bus.write(0x04800000, 4, 0x2000);
            bus.write(0x04800004, 4, 0x1fc007c0);
            bus.tick(1000);
            bus.set_interrupt(4, true);
            system.cpu.request_nmi();
            system.cpu.step();
            CHECK_EQ(bus.read(0x04800018, 4) & 1U, 1U);
            CHECK_EQ(bus.read(0x04300008, 4), 16U);
            const u64 remaining = cpu_clock ? (36020U * 3U - 2U + 1U) / 2U : 36020U;
            const auto advance = [&](u64 cycles) {
                const auto step = [&](u64 amount) { cpu_clock ? system.advance(amount) : bus.tick(amount); };
                if (single)
                    for (u64 i = 0; i < cycles; ++i)
                        step(1);
                else
                    step(cycles);
            };
            advance(remaining - 1);
            CHECK_EQ(bus.read(0x04800018, 4) & 1U, 1U);
            CHECK_EQ(bus.read_ram_byte(0x2003), 0U);
            advance(1);
            CHECK_EQ(bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
            CHECK_EQ(bus.read(0x04300008, 4), 18U);
            CHECK_EQ(bus.read_ram_byte(0x2003), 0x80U);
        }
}

TEST(cpu_nmi_does_not_cancel_eeprom_programming_or_reset_saved_data) {
    System system;
    prepare(system);
    auto& bus = system.bus;
    bus.set_save_type(SaveType::Eeprom4K);
    bus.controller_paks[2][31] = 0x56;
    std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
    bus.pif[0x7c4] = 3;
    bus.pif[0x7c5] = 1;
    bus.pif[0x7c6] = 5;
    bus.pif[0x7c8] = 0x12;
    bus.pif[0x7ca] = 0xfe;
    bus.joybus.configure();
    bus.joybus.execute();
    bus.tick(374999);
    CHECK_EQ(eeprom_status(bus), 0x80U);
    system.cpu.request_nmi();
    system.cpu.step();
    CHECK_EQ(eeprom_status(bus), 0x80U);
    CHECK_EQ(bus.eeprom[0], 0x12U);
    CHECK_EQ(bus.controller_paks[2][31], 0x56U);
    bus.tick(1);
    CHECK_EQ(eeprom_status(bus), 0U);
}

TEST(cpu_nmi_raised_by_an_audio_event_is_taken_at_the_next_instruction_boundary) {
    System system;
    prepare(system);
    auto& bus = system.bus;
    system.cpu.set_pc(0xffffffffa0001000ULL);
    bus.write(0x1000, 4, 0x34020042);
    unsigned callbacks = 0;
    bus.audio_output = [&](s16, s16) {
        ++callbacks;
        system.cpu.request_nmi();
    };
    bus.write(0x04500010, 4, 0);
    bus.write(0x04500008, 4, 1);
    bus.write(0x04500000, 4, 0x2000);
    bus.write(0x04500004, 4, 8);
    system.cpu.step();
    CHECK(callbacks != 0);
    CHECK_EQ(system.cpu.gpr[2], 0x42U);
    CHECK_EQ(system.cpu.pc, 0xffffffffa0001004ULL);
    system.cpu.step();
    CHECK_EQ(system.cpu.pc, 0xffffffffbfc00000ULL);
    CHECK_EQ(system.cpu.cp0[30], 0xffffffffa0001004ULL);
    CHECK_EQ(system.cpu.instruction_count, 1U);
}
