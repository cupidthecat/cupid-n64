#include "cupid/bus.hpp"

#include "cupid/system.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace cupid {
namespace {

constexpr u32 PifBase = 0x1fc00000U;
constexpr u32 PifRamOffset = 0x7c0U;

u8 byte_from_value(u64 value, unsigned width, unsigned index) {
    return static_cast<u8>(value >> ((width - index - 1U) * 8U));
}

} // namespace

Bus::Bus(System& system) : rdram(8U * 1024U * 1024U), rdp(*this), system_(system) {
    reset();
}

void Bus::reset() {
    memory.reset();

    std::fill(pif.begin() + PifRamOffset, pif.end(), u8{0});
    pif[0x7e5] = 0x04;
    pif[0x7e6] = cic.seed();
    pif[0x7e7] = cic.seed();

    open_bus_ = 0;
    mi_mode_ = 0;
    mi_interrupt_ = 0;
    mi_mask_ = 0;
    ri_.fill(0);
    ri_current_loaded_ = false;
    ri_refresh_counter_ = 0;
    vi_.fill(0);
    vi_[3] = 256;
    vi_[7] = 2047;
    ai_.fill(0);
    pi_.fill(0);
    si_.fill(0);

    vi_counter_ = 0;
    vi_line_period_.reset();
    vi_clock_fraction_ = 0;
    ai_counter_ = 0;
    ai_clock_rate_ = 44100;
    ai_clock_period_ = 62500000;
    ai_address_carry_ = false;
    pi_dma_counter_ = 0;
    pi_io_counter_ = 0;
    si_dma_counter_ = 0;
    si_io_counter_ = 0;
    eeprom_busy_counter_ = 0;
    flash_busy_counter_ = 0;
    vi_current_ = 0;
    vi_leap_counter_ = 0;
    vi_field_sequence_ = 0;
    ai_fifo_count_ = 0;
    ai_addresses_.fill(0);
    ai_lengths_.fill(0);
    pi_dma_cart_to_dram_ = false;
    pi_dma_pending_ = false;
    si_dma_pif_to_dram_ = false;
    si_dma_pending_ = false;
    pi_io_busy_ = false;
    pi_dma_busy_ = false;
    pi_error_ = false;
    pi_interrupt_ = false;
    si_interrupt_ = false;
    si_dma_busy_ = false;
    si_io_busy_ = false;
    si_bus_latch_ = 0;
    si_phase_ = 0;
    pi_bus_latch_ = 0;
    cart_device_ = CartDevice::Open;
    cart_offset_ = 0;
    cart_limit_ = 0;
    pif_rom_locked_ = false;
    pif_boot_terminated_ = false;
    pif_cpu_checksum_.fill(0);
    pif_checksum_valid_ = false;

    flash_mode_ = FlashMode::ReadArray;
    flash_erase_ = FlashErase::None;
    flash_sector_ = 0;
    flash_page_.fill(0xff);
    flash_status_ = 0x8c;
    flash_status_commands_ = 0;
    flash_command_high_ = 0;
    flash_previous_read_ = 0;
    flash_burst_index_ = 0;
    flash_command_high_valid_ = false;
    flash_status_stale_ = false;
    flash_open_bus_ = false;

    rdp.reset();
}

u64 Bus::read_bytes(const u8* data, std::size_t size, u32 offset, unsigned width) const {
    u64 value = 0;
    for (unsigned index = 0; index < width; ++index) {
        const std::size_t pos = static_cast<std::size_t>(offset) + index;
        value = (value << 8) | (pos < size ? data[pos] : 0U);
    }
    return value;
}

void Bus::write_bytes(u8* data, std::size_t size, u32 offset, unsigned width, u64 value) {
    for (unsigned index = 0; index < width; ++index) {
        const std::size_t pos = static_cast<std::size_t>(offset) + index;
        if (pos < size)
            data[pos] = byte_from_value(value, width, index);
    }
}

u32 Bus::read_word_be(const u8* data, std::size_t size, u32 offset) const {
    return static_cast<u32>(read_bytes(data, size, offset, 4));
}

void Bus::write_word_be(u8* data, std::size_t size, u32 offset, u32 value) {
    write_bytes(data, size, offset, 4, value);
}

u64 Bus::extract_word_lane(u32 word, u32 address, unsigned width) {
    if (width == 4)
        return word;
    if (width == 2)
        return static_cast<u16>(word >> ((2U - (address & 2U)) * 8U));
    if (width == 1)
        return static_cast<u8>(word >> ((3U - (address & 3U)) * 8U));
    return 0;
}

u32 Bus::expand_rcp_write(u32 address, unsigned width, u64 value) {
    const u32 data = static_cast<u32>(value);
    if (width == 4)
        return data;
    if (width == 2)
        return data << ((2U - (address & 2U)) * 8U);
    if (width == 1)
        return data << ((3U - (address & 3U)) * 8U);
    return static_cast<u32>(value >> 32);
}

u64 Bus::read_sp_memory(u32 physical, unsigned width) {
    if (width == 8)
        return 0;
    u64 value = 0;
    for (unsigned index = 0; index < width; ++index) {
        const u32 offset = (physical + index) & 0x1fffU;
        value = (value << 8) | system_.rsp.memory[offset];
    }
    return value;
}

void Bus::write_sp_memory(u32 physical, unsigned width, u64 value) {
    const u32 aligned = physical & ~3U;
    const u32 word = expand_rcp_write(physical, width, value);
    for (unsigned index = 0; index < 4; ++index) {
        system_.rsp.memory[(aligned + index) & 0x1fffU] = static_cast<u8>(word >> ((3U - index) * 8U));
    }
}

u32 Bus::read_pif_word(u32 address) const {
    const u32 offset = address & 0x7fcU;
    return offset < PifRamOffset && pif_rom_locked_ ? 0 : read_word_be(pif.data(), pif.size(), offset);
}

void Bus::write_pif_word(u32 physical, u32 value) {
    const u32 offset = (physical - PifBase) & 0x7ffU;
    if (offset < PifRamOffset)
        return;
    const u32 aligned = offset & ~3U;
    write_word_be(pif.data(), pif.size(), aligned, value);
}

u32 Bus::read_rcp_word(u32 physical) {
    if (physical >= 0x04040000U && physical <= 0x0407ffffU) {
        return system_.rsp.read_register(physical - 0x04040000U);
    }
    if (physical >= 0x04080000U && physical <= 0x040bffffU) {
        const u32 offset = (physical - 0x04080000U) & 0x1fU;
        if (offset == 0)
            return system_.rsp.pc & 0xffcU;
        if (offset == 4)
            return 0;
        return 0;
    }
    if (physical >= 0x04100000U && physical <= 0x041fffffU)
        return rdp.read_register(physical - 0x04100000U);
    if (physical >= 0x04200000U && physical <= 0x042fffffU)
        return rdp.read_test_register(physical - 0x04200000U);
    if (physical >= 0x04300000U && physical <= 0x043fffffU)
        return read_mi(physical - 0x04300000U);
    if (physical >= 0x04400000U && physical <= 0x044fffffU)
        return read_vi(physical - 0x04400000U);
    if (physical >= 0x04500000U && physical <= 0x045fffffU)
        return read_ai(physical - 0x04500000U);
    if (physical >= 0x04600000U && physical <= 0x046fffffU)
        return read_pi(physical - 0x04600000U);
    if (physical >= 0x04700000U && physical <= 0x047fffffU)
        return read_ri(physical - 0x04700000U);
    if (physical >= 0x04800000U && physical <= 0x048fffffU)
        return read_si(physical - 0x04800000U);
    return open_bus_;
}

void Bus::write_rcp_word(u32 physical, u32 value) {
    if (physical >= 0x04040000U && physical <= 0x0407ffffU) {
        system_.rsp.write_register(physical - 0x04040000U, value);
        return;
    }
    if (physical >= 0x04080000U && physical <= 0x040bffffU) {
        const u32 offset = (physical - 0x04080000U) & 0x1fU;
        if (offset == 0)
            system_.rsp.pc = value & 0xffcU;
        return;
    }
    if (physical >= 0x04100000U && physical <= 0x041fffffU) {
        rdp.write_register(physical - 0x04100000U, value);
        return;
    }
    if (physical >= 0x04200000U && physical <= 0x042fffffU) {
        rdp.write_test_register(physical - 0x04200000U, value);
        return;
    }
    if (physical >= 0x04300000U && physical <= 0x043fffffU) {
        write_mi(physical - 0x04300000U, value);
        return;
    }
    if (physical >= 0x04400000U && physical <= 0x044fffffU) {
        write_vi(physical - 0x04400000U, value);
        return;
    }
    if (physical >= 0x04500000U && physical <= 0x045fffffU) {
        write_ai(physical - 0x04500000U, value);
        return;
    }
    if (physical >= 0x04600000U && physical <= 0x046fffffU) {
        write_pi(physical - 0x04600000U, value);
        return;
    }
    if (physical >= 0x04700000U && physical <= 0x047fffffU) {
        write_ri(physical - 0x04700000U, value);
        return;
    }
    if (physical >= 0x04800000U && physical <= 0x048fffffU)
        write_si(physical - 0x04800000U, value);
}

u32 Bus::read_mi(u32 offset) const {
    switch ((offset & 0xfU) >> 2U) {
    case 0:
        return mi_mode_ & 0x3ffU;
    case 1:
        return 0x02020102U;
    case 2:
        return mi_interrupt_ & 0x3fU;
    case 3:
        return mi_mask_ & 0x3fU;
    default:
        return 0;
    }
}

void Bus::write_mi(u32 offset, u32 value) {
    switch ((offset & 0xfU) >> 2U) {
    case 0:
        mi_mode_ = (mi_mode_ & ~0x7fU) | (value & 0x7fU);
        if ((value & (1U << 7U)) != 0)
            mi_mode_ &= ~0x80U;
        if ((value & (1U << 8U)) != 0)
            mi_mode_ |= 0x80U;
        if ((value & (1U << 9U)) != 0)
            mi_mode_ &= ~0x100U;
        if ((value & (1U << 10U)) != 0)
            mi_mode_ |= 0x100U;
        if ((value & (1U << 11U)) != 0)
            set_interrupt(5, false);
        if ((value & (1U << 12U)) != 0)
            mi_mode_ &= ~0x200U;
        if ((value & (1U << 13U)) != 0)
            mi_mode_ |= 0x200U;
        return;
    case 3:
        for (unsigned source = 0; source < 6; ++source) {
            const u32 clear_bit = 1U << (source * 2U);
            const u32 set_bit = clear_bit << 1U;
            if ((value & clear_bit) != 0)
                mi_mask_ &= ~(1U << source);
            if ((value & set_bit) != 0)
                mi_mask_ |= 1U << source;
        }
        return;
    default:
        return;
    }
}

u32 Bus::read_pi(u32 offset) const {
    const unsigned index = static_cast<unsigned>((offset & 0x3fU) >> 2U);
    if (index == 4) {
        return (pi_dma_busy_ ? 1U : 0U) | (pi_io_busy_ ? 2U : 0U) | (pi_error_ ? 4U : 0U) |
               (pi_interrupt_ ? 8U : 0U);
    }
    if (index == 13 || index == 14)
        return pi_bus_latch_;
    return index < pi_.size() ? pi_[index] : 0;
}

void Bus::write_pi(u32 offset, u32 value) {
    const unsigned index = static_cast<unsigned>((offset & 0x3fU) >> 2U);
    if (index != 4 && (pi_dma_busy_ || pi_io_busy_)) {
        pi_error_ = true;
        return;
    }
    switch (index) {
    case 0:
        pi_[0] = value & 0x00fffffeU;
        return;
    case 1:
        pi_[1] = value & ~1U;
        return;
    case 2:
        pi_[2] = value & 0x00ffffffU;
        pi_dma_cart_to_dram_ = false;
        pi_dma_pending_ = true;
        pi_dma_busy_ = true;
        pi_dma_counter_ = pi_dma_cycles(pi_[2]);
        perform_pi_dma();
        return;
    case 3:
        pi_[3] = value & 0x00ffffffU;
        pi_dma_cart_to_dram_ = true;
        pi_dma_pending_ = true;
        pi_dma_busy_ = true;
        pi_dma_counter_ = pi_dma_cycles(pi_[3]);
        perform_pi_dma();
        return;
    case 4:
        if ((value & 1U) != 0) {
            pi_dma_busy_ = false;
            pi_dma_pending_ = false;
            pi_error_ = false;
            pi_dma_counter_ = 0;
        }
        if ((value & 2U) != 0) {
            pi_interrupt_ = false;
            set_interrupt(4, false);
        }
        return;
    default:
        if (index < pi_.size())
            pi_[index] = value & (index == 7 || index == 11 ? 0xfU : index == 8 || index == 12 ? 3U : 0xffU);
        return;
    }
}

void Bus::set_interrupt(unsigned source, bool level) {
    if (source >= 6)
        return;
    const u32 bit = 1U << source;
    if (level)
        mi_interrupt_ |= bit;
    else
        mi_interrupt_ &= ~bit;
}

bool Bus::interrupt_pending() const {
    return (mi_interrupt_ & mi_mask_) != 0;
}

void Bus::tick_devices(u64 rcp_cycles) {
    rdp.tick(rcp_cycles);
    ri_refresh_counter_ -= std::min(ri_refresh_counter_, rcp_cycles);
    tick_vi(rcp_cycles);
    if (rcp_cycles == 0)
        return;

    if (pi_io_busy_) {
        if (rcp_cycles >= pi_io_counter_) {
            pi_io_counter_ = 0;
            pi_io_busy_ = false;
        } else
            pi_io_counter_ -= rcp_cycles;
    }
    if (pi_dma_pending_) {
        if (rcp_cycles >= pi_dma_counter_) {
            pi_dma_counter_ = 0;
            finish_pi_dma();
        } else
            pi_dma_counter_ -= rcp_cycles;
    }
    tick_si(rcp_cycles);
    tick_flash(rcp_cycles);
    if (eeprom_busy_counter_ != 0) {
        if (rcp_cycles >= eeprom_busy_counter_)
            eeprom_busy_counter_ = 0;
        else
            eeprom_busy_counter_ -= rcp_cycles;
    }
    tick_ai(rcp_cycles);
}

bool Bus::load_rom(std::vector<u8> data, std::string& error) {
    if (data.size() < 4) {
        error = "The cartridge image is too small.";
        return false;
    }
    if (data[0] == 0x37 && data[1] == 0x80 && data[2] == 0x40 && data[3] == 0x12) {
        for (std::size_t index = 0; index + 1 < data.size(); index += 2)
            std::swap(data[index], data[index + 1]);
    } else if (data[0] == 0x40 && data[1] == 0x12 && data[2] == 0x37 && data[3] == 0x80) {
        for (std::size_t index = 0; index + 3 < data.size(); index += 4) {
            std::swap(data[index], data[index + 3]);
            std::swap(data[index + 1], data[index + 2]);
        }
    } else if (!(data[0] == 0x80 && data[1] == 0x37 && data[2] == 0x12 && data[3] == 0x40)) {
        error = "Unrecognized cartridge byte order.";
        return false;
    }

    rom = std::move(data);
    cic.detect(rom);
    pif[0x7e6] = cic.seed();
    pif[0x7e7] = cic.seed();
    error.clear();
    return true;
}

void Bus::set_save_type(SaveType type) {
    save_type = type;
    if (type == SaveType::Sram && sram.empty())
        sram.assign(32U * 1024U, 0);
    if (type == SaveType::FlashRam && flashram.empty())
        flashram.assign(128U * 1024U, 0xff);
    if (type == SaveType::Eeprom4K && eeprom.size() != 512)
        eeprom.assign(512, 0xff);
    if (type == SaveType::Eeprom16K && eeprom.size() != 2048)
        eeprom.assign(2048, 0xff);
}

void Bus::set_controller_state(unsigned port, ControllerState state) {
    if (port < controllers.size())
        controllers[port] = state;
}

void Bus::emit_isviewer() {
    if (!debug_output)
        return;
    const u16 start = static_cast<u16>(read_bytes(isviewer_.data(), isviewer_.size(), 4, 2));
    const u16 count = static_cast<u16>(read_bytes(isviewer_.data(), isviewer_.size(), 0x16, 2));
    std::string text;
    text.reserve(count);
    for (u32 index = 0; index < count; ++index) {
        const u32 address = 0x20U + static_cast<u32>(start) + index;
        if (address < isviewer_.size())
            text.push_back(static_cast<char>(isviewer_[address]));
    }
    if (!text.empty())
        debug_output(text);
    write_word_be(isviewer_.data(), isviewer_.size(), 4, 0);
}

void Bus::perform_pi_dma() {
    if (!pi_dma_pending_)
        return;
    const u32 page_mask = pi_page_mask(pi_[1]);
    if (pi_dma_cart_to_dram_) {
        std::array<u8, 128> buffer{};
        s32 length = static_cast<s32>(pi_[3] + 1U);
        s32 max_block_size = 128;
        bool first_block = true;
        bool selected = false;
        while (length > 0) {
            const s32 misalign = static_cast<s32>(pi_[0] & 7U);
            const s32 distance_to_row = 0x800 - static_cast<s32>(pi_[0] & 0x7ffU);
            const s32 block_length = std::min(max_block_size - misalign, distance_to_row);
            const s32 current_length = std::min(length, block_length);
            for (s32 index = 0; index < current_length; index += 2) {
                if (!selected || (pi_[1] & page_mask) == 0) {
                    select_cart(pi_[1]);
                    selected = true;
                }
                const u16 data = cart_read_half();
                buffer[static_cast<std::size_t>(index)] = static_cast<u8>(data >> 8U);
                if (index + 1 < static_cast<s32>(buffer.size()))
                    buffer[static_cast<std::size_t>(index + 1)] = static_cast<u8>(data);
                pi_[1] += 2U;
                length -= 2;
            }
            const s32 write_length = std::max<s32>(0, current_length - misalign);
            if (first_block && current_length < 127 - misalign) {
                for (s32 index = 0; index < write_length; ++index) {
                    write_ram_byte(pi_[0]++, buffer[static_cast<std::size_t>(index)]);
                }
            } else {
                for (s32 index = 0; index < write_length; index += 2) {
                    write_ram_byte(pi_[0]++, buffer[static_cast<std::size_t>(index)]);
                    write_ram_byte(pi_[0]++, buffer[static_cast<std::size_t>(index + 1)]);
                }
            }
            pi_[0] = (pi_[0] + 7U) & ~7U;
            pi_[3] = static_cast<u32>(current_length <= 8 ? 127 - misalign : 127);
            first_block = false;
            max_block_size = distance_to_row < 8 ? 128 - misalign : 128;
        }
    } else {
        const u32 length = (pi_[2] | 1U) + 1U;
        pi_[2] = length;
        select_cart(pi_[1]);
        for (u32 index = 0; index < length; index += 2U) {
            if (index != 0 && ((pi_[1] + index) & page_mask) == 0)
                select_cart(pi_[1] + index);
            const u16 data = static_cast<u16>((static_cast<u16>(read_ram_byte(pi_[0] + index)) << 8U) |
                                              read_ram_byte(pi_[0] + index + 1U));
            cart_write_half(data);
        }
    }
}

void Bus::finish_pi_dma() {
    if (!pi_dma_pending_)
        return;
    pi_dma_pending_ = false;
    pi_dma_busy_ = false;
    pi_interrupt_ = true;
    set_interrupt(4, true);
}

u8 Bus::address_crc(u16 address) {
    u8 crc = 0;
    for (unsigned index = 0; index < 16; ++index) {
        const u8 feedback = (crc & 0x10U) != 0 ? 0x15U : 0U;
        crc = static_cast<u8>((static_cast<unsigned>(crc) << 1U) | ((address & 0x8000U) != 0 ? 1U : 0U));
        address = static_cast<u16>(address << 1U);
        crc ^= feedback;
    }
    return static_cast<u8>(crc & 0x1fU);
}

u8 Bus::pak_crc(const u8* data) {
    u8 crc = 0;
    for (unsigned byte = 0; byte < 33; ++byte) {
        for (int bit = 7; bit >= 0; --bit) {
            const u8 feedback = (crc & 0x80U) != 0 ? 0x85U : 0U;
            crc = static_cast<u8>(crc << 1U);
            if (byte < 32 && (data[byte] & (1U << static_cast<unsigned>(bit))) != 0)
                crc |= 1U;
            crc ^= feedback;
        }
    }
    return crc;
}

void Bus::process_pif() {
    if ((pif[0x7ff] & 0x02U) != 0) {
        std::span<u8, 15> challenge(pif.data() + PifRamOffset + 0x30, 15);
        cic.challenge(challenge);
        return;
    }

    u32 offset = 0;
    unsigned channel = 0;
    while (channel < 5 && offset < 63) {
        u8 send = pif[PifRamOffset + offset++];
        if (send == 0xfe)
            break;
        if (send == 0xff)
            continue;
        if (send == 0 || send == 0xfd) {
            ++channel;
            continue;
        }
        if (offset >= 63)
            break;
        const u32 recv_offset = offset;
        u8 recv = pif[PifRamOffset + offset++];
        if ((send & 0x80U) != 0) {
            ++channel;
            continue;
        }
        send &= 0x3fU;
        recv &= 0x3fU;
        if (offset + send + recv > 64U)
            break;
        const u8* input = pif.data() + PifRamOffset + offset;
        u8* output = pif.data() + PifRamOffset + offset + send;
        bool valid = false;
        bool overflow = false;
        execute_joybus(channel, send, recv, input, output, valid, overflow);
        if (!valid)
            pif[PifRamOffset + recv_offset] = static_cast<u8>(recv | 0x80U);
        else if (overflow)
            pif[PifRamOffset + recv_offset] = static_cast<u8>(recv | 0x40U);
        else
            pif[PifRamOffset + recv_offset] = recv;
        offset += static_cast<u32>(send) + recv;
        ++channel;
    }
    pif[0x7ff] &= static_cast<u8>(~1U);
}

void Bus::execute_joybus(unsigned channel, u8 send, u8 recv, const u8* input, u8* output, bool& valid,
                         bool& overflow) {
    overflow = false;
    if (send == 0)
        return;
    const u8 command = input[0];
    if (channel < 4) {
        const ControllerState& controller = controllers[channel];
        if (!controller.connected)
            return;
        if ((command == 0x00 || command == 0xff) && recv >= 3) {
            output[0] = 0x05;
            output[1] = 0x00;
            output[2] = controller.controller_pak ? 0x01 : 0x02;
            valid = true;
            return;
        }
        if (command == 0x01 && recv >= 4) {
            output[0] = static_cast<u8>(controller.buttons >> 8U);
            output[1] = static_cast<u8>(controller.buttons);
            output[2] = static_cast<u8>(controller.stick_x);
            output[3] = static_cast<u8>(controller.stick_y);
            overflow = recv > 4;
            valid = true;
            return;
        }
        if (command == 0x02 && send >= 3 && recv >= 1) {
            const u16 encoded = static_cast<u16>((static_cast<u16>(input[1]) << 8U) | input[2]);
            const u16 address = encoded & 0xffe0U;
            const unsigned data_length = std::min<unsigned>(recv, 32);
            const bool accessible = controller.controller_pak && (encoded & 0x1fU) == address_crc(address);
            for (unsigned index = 0; index < std::min<unsigned>(recv, 33); ++index)
                output[index] = 0;
            if (accessible) {
                for (unsigned index = 0; index < data_length; ++index) {
                    const u32 pos = static_cast<u32>(address) + index;
                    output[index] = pos < controller_paks[channel].size() ? controller_paks[channel][pos] : 0;
                }
            }
            valid = true;
            if (recv >= 33) {
                output[32] = pak_crc(output);
                if (!accessible)
                    output[32] ^= 0xffU;
            }
            return;
        }
        if (command == 0x03 && send >= 4 && recv >= 1) {
            const u16 encoded = static_cast<u16>((static_cast<u16>(input[1]) << 8U) | input[2]);
            const u16 address = encoded & 0xffe0U;
            const unsigned data_length = std::min<unsigned>(send - 3U, 32);
            const bool accessible = controller.controller_pak && (encoded & 0x1fU) == address_crc(address);
            if (accessible && address != 0x8000U) {
                for (unsigned index = 0; index < data_length; ++index) {
                    const u32 pos = static_cast<u32>(address) + index;
                    if (pos < controller_paks[channel].size())
                        controller_paks[channel][pos] = input[3 + index];
                }
            }
            output[0] = data_length == 32 ? pak_crc(input + 3) : 0;
            if (!accessible)
                output[0] ^= 0xffU;
            valid = true;
        }
        return;
    }

    if (channel != 4 || (save_type != SaveType::Eeprom4K && save_type != SaveType::Eeprom16K) ||
        eeprom.empty())
        return;
    if ((command == 0x00 || command == 0xff) && recv >= 3) {
        output[0] = 0;
        output[1] = eeprom.size() == 512 ? 0x80 : 0xc0;
        output[2] = eeprom_busy_counter_ != 0 ? 0x80 : 0;
        valid = true;
        return;
    }
    if (command == 0x04 && send >= 2) {
        const u32 address = static_cast<u32>(input[1]) * 8U;
        for (unsigned index = 0; index < recv; ++index) {
            const u32 pos = address + index;
            output[index] = eeprom_busy_counter_ == 0 && pos < eeprom.size() ? eeprom[pos] : 0xff;
        }
        valid = true;
        return;
    }
    if (command == 0x05 && send >= 2 && recv >= 1) {
        output[0] = eeprom_busy_counter_ != 0 ? 0x80 : 0;
        valid = true;
        if (eeprom_busy_counter_ == 0) {
            const u32 address = static_cast<u32>(input[1]) * 8U;
            for (unsigned index = 0; index + 2U < send; ++index) {
                const u32 pos = address + index;
                if (pos < eeprom.size())
                    eeprom[pos] = input[2 + index];
            }
            eeprom_busy_counter_ = 375000;
        }
    }
}

void Bus::process_pif_control() {
    u8& command = pif[0x7ff];
    if ((command & 0x10U) != 0)
        pif_rom_locked_ = true;
    if ((command & 0x20U) != 0) {
        std::copy_n(pif.begin() + 0x7f2, pif_cpu_checksum_.size(), pif_cpu_checksum_.begin());
        std::fill(pif.begin() + 0x7f2, pif.begin() + 0x7f8, u8{0});
        pif[0x7e5] = 0;
        pif[0x7e6] = 0;
        pif[0x7e7] = 0;
        command |= 0x80U;
    }
    if ((command & 0x40U) != 0) {
        pif_checksum_valid_ = cic.verify_checksum(pif_cpu_checksum_);
    }
    if ((command & 0x08U) != 0) {
        pif_boot_terminated_ = true;
        command = 0;
        return;
    }
    if ((command & 0x01U) != 0)
        process_pif();
}

u8 Bus::rdp_source_byte(u32 address, bool dmem) const {
    if (dmem)
        return system_.rsp.memory[address & 0xfffU];
    return read_ram_byte(address);
}

} // namespace cupid
