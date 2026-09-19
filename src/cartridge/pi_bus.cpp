#include "cupid/bus.hpp"

namespace cupid {
namespace {
constexpr u32 rom_base = 0x10000000U;
constexpr u32 save_base = 0x08000000U;
constexpr u32 isviewer_base = 0x13ff0000U;

u32 repeat_half(u16 value) {
    return (static_cast<u32>(value) << 16) | value;
}
} // namespace

void Bus::select_cart(u32 physical) {
    physical &= ~1U;
    pi_bus_latch_ = repeat_half(static_cast<u16>(physical));
    cart_device_ = CartDevice::Open;
    cart_offset_ = 0;
    cart_limit_ = 0;
    if (physical >= isviewer_base && physical <= isviewer_base + 0xffffU) {
        cart_device_ = CartDevice::IsViewer;
        cart_offset_ = physical - isviewer_base;
    } else if (physical >= rom_base) {
        const u32 offset = physical - rom_base;
        if (offset < rom.size()) {
            cart_device_ = CartDevice::Rom;
            cart_offset_ = offset;
        }
    } else if (physical >= save_base) {
        const u32 offset = physical - save_base;
        if (save_type == SaveType::Sram && !sram.empty()) {
            if (sram.size() > 0x8000) {
                const u32 bank = offset >> 18;
                if (bank >= sram.size() / 0x8000 || (offset & 0x3ffffU) >= 0x8000)
                    return;
                cart_offset_ = bank * 0x8000 + (offset & 0x7fffU);
                cart_limit_ = (bank + 1) * 0x8000;
            } else {
                cart_limit_ = static_cast<u32>(sram.size());
                cart_offset_ = offset % cart_limit_;
            }
            cart_device_ = CartDevice::Sram;
        } else if (save_type == SaveType::FlashRam && !flashram.empty()) {
            cart_device_ = CartDevice::Flash;
            cart_offset_ = offset;
            flash_burst_index_ = 0;
            flash_command_high_valid_ = false;
        }
    }
}

u64 Bus::read_cart(u32 physical, unsigned width) {
    if (pi_io_busy_) {
        pi_io_busy_ = false;
        pi_io_counter_ = 0;
        return extract_word_lane(pi_bus_latch_, physical, width);
    }
    select_cart(physical);
    const u16 high = cart_read_half();
    const u16 low = cart_read_half();
    pi_bus_latch_ = (static_cast<u32>(high) << 16) | low;
    pi_[1] = (physical + 4U) & ~1U;
    open_bus_ = pi_bus_latch_;
    return extract_word_lane(pi_bus_latch_, physical, width);
}

void Bus::write_cart(u32 physical, unsigned width, u64 value) {
    if (pi_io_busy_)
        return;
    select_cart(physical);
    const u32 word = expand_rcp_write(physical, width, value);
    pi_io_busy_ = true;
    pi_io_counter_ = 140;
    pi_[1] = (physical + 4U) & ~1U;
    pi_bus_latch_ = word;
    cart_write_half(static_cast<u16>(word >> 16));
    cart_write_half(static_cast<u16>(word));
}

u16 Bus::cart_read_half() {
    u16 value = 0;
    switch (cart_device_) {
    case CartDevice::Open:
        return static_cast<u16>(pi_bus_latch_);
    case CartDevice::Rom:
        if (static_cast<u64>(cart_offset_) + 1 >= rom.size())
            return static_cast<u16>(pi_bus_latch_);
        value = static_cast<u16>(read_bytes(rom.data(), rom.size(), cart_offset_, 2));
        break;
    case CartDevice::Sram:
        if (static_cast<u64>(cart_offset_) + 1 >= cart_limit_)
            return static_cast<u16>(pi_bus_latch_);
        value = static_cast<u16>(read_bytes(sram.data(), sram.size(), cart_offset_, 2));
        break;
    case CartDevice::IsViewer:
        value = static_cast<u16>(read_bytes(isviewer_.data(), isviewer_.size(), cart_offset_ & 0xffffU, 2));
        break;
    case CartDevice::Flash:
        if (const auto data = read_flash_half())
            pi_bus_latch_ = repeat_half(*data);
        return static_cast<u16>(pi_bus_latch_);
    }
    cart_offset_ += 2;
    pi_bus_latch_ = repeat_half(value);
    return value;
}

void Bus::cart_write_half(u16 value) {
    if (!pi_io_busy_)
        pi_bus_latch_ = repeat_half(value);
    switch (cart_device_) {
    case CartDevice::Open:
        return;
    case CartDevice::Rom:
        break;
    case CartDevice::Sram:
        if (static_cast<u64>(cart_offset_) + 1 >= cart_limit_)
            return;
        write_bytes(sram.data(), sram.size(), cart_offset_, 2, value);
        break;
    case CartDevice::IsViewer:
        pi_io_busy_ = false;
        pi_io_counter_ = 0;
        write_bytes(isviewer_.data(), isviewer_.size(), cart_offset_ & 0xffffU, 2, value);
        if ((cart_offset_ & 0xffffU) == 0x16U)
            emit_isviewer();
        break;
    case CartDevice::Flash:
        write_flash_half(value);
        return;
    }
    cart_offset_ += 2;
}

} // namespace cupid
