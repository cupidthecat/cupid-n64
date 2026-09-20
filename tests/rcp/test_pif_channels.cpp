#include "cupid/system.hpp"
#include "test.hpp"
#include "test_system.hpp"

#include <algorithm>

namespace {
using namespace cupid;

struct Channels {
    System system;
    Bus& bus{system.bus};

    Channels() {
        test::initialize_memory(system);
        bus.write(0x04700010, 4, 0);
        clear();
    }
    void clear() {
        std::fill(bus.pif.begin() + 0x7c0, bus.pif.end(), u8{0});
    }
    void poll(unsigned offset = 0, u8 command = 1) {
        bus.pif[0x7c0 + offset] = 1;
        bus.pif[0x7c1 + offset] = 4;
        bus.pif[0x7c2 + offset] = command;
        std::fill_n(bus.pif.begin() + 0x7c3 + offset, 4, u8{0xcc});
        bus.pif[0x7c7 + offset] = 0xfe;
    }
    void configure() {
        bus.write(0x1fc007fc, 4, 1);
        static_cast<void>(bus.read(0x1fc007fc, 4));
    }
    void read() {
        bus.write(0x04800000, 4, 0x2000);
        bus.write(0x04800004, 4, 0x1fc007c0);
        while ((bus.read(0x04800018, 4) & 1U) != 0)
            bus.tick(100);
        bus.write(0x04800018, 4, 0);
    }
};
} // namespace

TEST(pif_channels_write_configures_without_consuming_mouse_motion) {
    Channels f;
    ControllerState state;
    state.device = ControllerDevice::Mouse;
    f.bus.set_controller_state(0, state);
    f.bus.add_mouse_input(0, {false, false, 12, -9});
    f.poll();
    f.configure();
    CHECK_EQ(f.bus.pif[0x7ff], 0U);
    CHECK_EQ(f.bus.pif[0x7c5], 0xccU);
    f.read();
    CHECK_EQ(f.bus.pif[0x7c5], 12U);
    CHECK_EQ(f.bus.pif[0x7c6], 9U);
    f.read();
    CHECK_EQ(f.bus.pif[0x7c5], 0U);
    CHECK_EQ(f.bus.pif[0x7c6], 0U);
}

TEST(pif_channels_unconfigured_read_does_not_discover_packets) {
    Channels f;
    f.poll();
    f.bus.pif[0x7ff] = 1;
    f.read();
    CHECK_EQ(f.bus.pif[0x7c3], 0xccU);
    CHECK_EQ(f.bus.pif[0x7ff], 1U);
    f.configure();
    f.read();
    CHECK_EQ(f.bus.pif[0x7c3], 0U);
}

TEST(pif_channels_offsets_survive_unconfigured_packet_layout_changes) {
    Channels f;
    ControllerState state;
    state.buttons = 0x8000;
    f.bus.set_controller_state(0, state);
    state.buttons = 0x4000;
    f.bus.set_controller_state(1, state);
    f.poll(1);
    f.configure();
    f.bus.pif[0x7c0] = 0xff;
    f.read();
    CHECK_EQ(f.bus.pif[0x7c4], 0x40U);
    f.configure();
    f.read();
    CHECK_EQ(f.bus.pif[0x7c4], 0x80U);
}

TEST(pif_channels_write_dma_does_not_acknowledge_controller_attachment) {
    Channels f;
    f.poll(0, 0);
    f.bus.pif[0x7ff] = 1;
    for (u32 i = 0; i < 64; ++i)
        f.bus.write_ram_byte(0x2000 + i, f.bus.pif[0x7c0 + i]);
    f.clear();
    f.bus.write(0x04800000, 4, 0x2000);
    f.bus.write(0x04800010, 4, 0x1fc007c0);
    f.bus.tick(4065);
    CHECK_EQ(f.bus.pif[0x7c5], 0xccU);
    f.bus.write(0x04800018, 4, 0);
    f.read();
    CHECK_EQ(f.bus.pif[0x7c5], 3U);
    f.read();
    CHECK_EQ(f.bus.pif[0x7c5], 1U);
}

TEST(pif_channels_read_uses_current_lengths_commands_flags_and_inputs) {
    Channels f;
    f.poll();
    f.configure();
    ControllerState state;
    state.buttons = 0x8000;
    f.bus.set_controller_state(0, state);
    for (u8 flags : {u8{0x40}, u8{0x80}, u8{0xc0}}) {
        f.bus.pif[0x7c0] = flags | 1U;
        f.bus.pif[0x7c1] = 0xc4;
        f.read();
        CHECK_EQ(f.bus.pif[0x7c1], 0xc4U);
        CHECK_EQ(f.bus.pif[0x7c3], 0xccU);
    }
    f.bus.pif[0x7c0] = 1;
    f.bus.pif[0x7c1] = 2;
    f.read();
    CHECK_EQ(f.bus.pif[0x7c3], 0x80U);
    CHECK_EQ(f.bus.pif[0x7c5], 0xccU);
    f.bus.pif[0x7c2] = 0;
    f.read();
    CHECK_EQ(f.bus.pif[0x7c3], 5U);
    CHECK_EQ(f.bus.pif[0x7c4], 0U);
}

TEST(pif_channels_reconfiguration_disables_previous_descriptors) {
    Channels f;
    f.poll();
    f.configure();
    f.bus.pif[0x7c0] = 0xfe;
    f.configure();
    f.poll();
    f.read();
    CHECK_EQ(f.bus.pif[0x7c3], 0xccU);
    f.configure();
    f.read();
    CHECK_EQ(f.bus.pif[0x7c3], 0U);
    f.system.reset();
    test::initialize_memory(f.system);
    f.poll();
    f.read();
    CHECK_EQ(f.bus.pif[0x7c3], 0xccU);
}

TEST(pif_channels_changed_lengths_cannot_overrun_pif_ram) {
    for (unsigned send = 0; send < 64; ++send)
        for (unsigned recv = 0; recv < 64; ++recv) {
            if (send + recv < 7)
                continue;
            Channels f;
            std::fill(f.bus.pif.begin() + 0x7c0, f.bus.pif.begin() + 0x7f7, u8{0xff});
            f.bus.pif[0x7f7] = 1;
            f.bus.pif[0x7f8] = 3;
            f.bus.pif[0x7f9] = 0;
            f.configure();
            f.bus.pif[0x7f7] = static_cast<u8>(send);
            f.bus.pif[0x7f8] = static_cast<u8>(recv | 0xc0U);
            const auto before = f.bus.pif;
            f.bus.joybus.execute();
            CHECK(f.bus.pif == before);
        }
}

TEST(pif_channels_execute_higher_channels_before_overlapping_lower_replies) {
    Channels f;
    ControllerState state;
    state.device = ControllerDevice::Mouse;
    f.bus.set_controller_state(1, state);
    f.bus.add_mouse_input(1, {false, false, 13, 0});
    f.poll(0, 0);
    f.poll(7);
    f.configure();
    f.bus.pif[0x7c1] = 12;
    f.read();
    CHECK_EQ(f.bus.pif[0x7c3], 5U);
    f.clear();
    f.poll(1);
    f.configure();
    f.read();
    CHECK_EQ(f.bus.pif[0x7c6], 0U);
}

TEST(pif_channels_challenge_reads_preserve_the_channel_configuration) {
    Channels f;
    f.bus.cic.configure(CicModel::Nus6105);
    f.poll();
    f.configure();
    f.bus.pif[0x7ff] = 2;
    std::fill(f.bus.pif.begin() + 0x7f0, f.bus.pif.begin() + 0x7ff, u8{0});
    f.read();
    CHECK_EQ(f.bus.pif[0x7c3], 0xccU);
    CHECK_EQ(f.bus.pif[0x7f0], 0xbfU);
    f.bus.pif[0x7ff] = 0;
    f.read();
    CHECK_EQ(f.bus.pif[0x7c3], 0U);
}

TEST(pif_channels_si_write_and_read_boundaries_preserve_inputs_in_both_clock_domains) {
    for (unsigned port = 0; port < 4; ++port)
        for (bool cpu : {false, true})
            for (bool single : {false, true}) {
                Channels f;
                ControllerState state;
                state.device = ControllerDevice::Mouse;
                f.bus.set_controller_state(port, state);
                f.bus.add_mouse_input(port, {false, false, 12, 0});
                f.poll(port);
                f.bus.pif[0x7ff] = 1;
                for (u32 i = 0; i < 64; ++i)
                    f.bus.write_ram_byte(0x2000 + i, f.bus.pif[0x7c0 + i]);
                f.clear();
                u64 fraction = 0;
                const auto advance = [&](u64 cycles) {
                    const auto step = [&](u64 amount) {
                        cpu ? f.system.advance(amount) : f.bus.tick(amount);
                    };
                    if (single)
                        for (u64 i = 0; i < cycles; ++i)
                            step(1);
                    else
                        step(cycles);
                };
                const auto cycles_for = [&](u64 rcp) {
                    const u64 cycles = cpu ? (rcp * 3 - fraction + 1) / 2 : rcp;
                    if (cpu)
                        fraction = (fraction + cycles * 2) % 3;
                    return cycles;
                };
                f.bus.write(0x04800000, 4, 0x2000);
                f.bus.write(0x04800010, 4, 0x1fc007c0);
                advance(cycles_for(4065) - 1);
                CHECK_EQ(f.bus.pif[0x7c5 + port], 0U);
                CHECK_EQ(f.bus.read(0x04800018, 4) & 1U, 1U);
                f.bus.add_mouse_input(port, {false, false, 3, 0});
                advance(1);
                CHECK_EQ(f.bus.pif[0x7c5 + port], 0xccU);
                CHECK_EQ(f.bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
                f.bus.write(0x04800018, 4, 0);
                f.bus.add_mouse_input(port, {false, false, 4, 0});
                f.bus.write(0x04800004, 4, 0x1fc007c0);
                advance(cycles_for(37020 + port * 1420) - 1);
                CHECK_EQ(f.bus.pif[0x7c5 + port], 0xccU);
                CHECK_EQ(f.bus.read(0x04800018, 4) & 1U, 1U);
                f.bus.add_mouse_input(port, {false, false, 5, 0});
                advance(1);
                CHECK_EQ(f.bus.pif[0x7c5 + port], 24U);
                CHECK_EQ(f.bus.read_ram_byte(0x2005 + port), 24U);
                CHECK_EQ(f.bus.read(0x04800018, 4) & 0x1001U, 0x1000U);
                f.bus.write(0x04800018, 4, 0);
                f.read();
                CHECK_EQ(f.bus.pif[0x7c5 + port], 0U);
            }
}
