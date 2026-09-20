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
    joybus.reset();
    pending_outputs_.clear();
    memory.reset();

    std::fill(pif.begin() + PifRamOffset, pif.end(), u8{0});
    pif_boot.reset();

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
    reset_ai_clock();
    ai_address_carry_ = false;
    pi_dma_counter_ = 0;
    pi_io_counter_ = 0;
    si_dma_counter_ = 0;
    si_io_counter_ = 0;
    eeprom_busy_counter_ = 0;
    if (rtc)
        rtc->reset_clock();
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

    reset_flash();

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
    return offset < PifRamOffset && pif_boot.rom_locked() ? 0 : read_word_be(pif.data(), pif.size(), offset);
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
    if (rcp_cycles != 0)
        ai_clock_started_ = true;
    rdp.tick(rcp_cycles);
    ri_refresh_counter_ -= std::min(ri_refresh_counter_, rcp_cycles);
    tick_vi(rcp_cycles);
    if (rcp_cycles == 0)
        return;
    pif_boot.tick(rcp_cycles);

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
    tick_eeprom(rcp_cycles);
    if (rtc)
        rtc->tick(rcp_cycles);
    for (auto& pak : transfer_paks)
        if (auto* cartridge = pak.cartridge())
            cartridge->tick(rcp_cycles);
    tick_si(rcp_cycles);
    tick_flash(rcp_cycles);
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

void Bus::process_pif_control() {
    if (pif_boot.failed())
        return;
    u8& command = pif[0x7ff];
    if ((command & 0x01U) != 0) {
        command &= static_cast<u8>(~1U);
        joybus.configure();
    }
}

u8 Bus::rdp_source_byte(u32 address, bool dmem) const {
    if (dmem)
        return system_.rsp.memory[address & 0xfffU];
    return read_ram_byte(address);
}

} // namespace cupid
