#include "cupid/bus.hpp"

#include <algorithm>

namespace cupid {

std::optional<u16> Bus::read_flash_half() {
    if (flash_open_bus_)
        return std::nullopt;
    u16 value = 0;
    switch (flash_mode_) {
    case FlashMode::Status:
        if ((cart_offset_ & 0x20000U) != 0) {
            flash_open_bus_ = true;
            return std::nullopt;
        }
        value = flash_status_stale_ ? flash_previous_read_ : flash_status_;
        flash_status_stale_ = false;
        break;
    case FlashMode::SiliconId: {
        constexpr std::array<u16, 4> id{0x1111, 0x8001, 0x00c2, 0x001e};
        value = id[flash_burst_index_ % id.size()];
        break;
    }
    case FlashMode::LoadPage:
        value = static_cast<u16>(read_bytes(flash_page_.data(), flash_page_.size(), cart_offset_ & 0x7eU, 2));
        break;
    case FlashMode::ReadArray:
        value = static_cast<u16>(read_bytes(flashram.data(), flashram.size(), cart_offset_ << 1, 2));
        // The MX29L1100 advances a halfword index within its 32 KiB burst window.
        cart_offset_ = (cart_offset_ & ~0x3fffU) | ((cart_offset_ + 1) & 0x3fffU);
        flash_previous_read_ = value;
        return value;
    }
    cart_offset_ += 2;
    ++flash_burst_index_;
    flash_previous_read_ = value;
    return value;
}

void Bus::write_flash_half(u16 value) {
    const u32 offset = cart_offset_ & ~1U;
    cart_offset_ += 2;
    if (offset == 0x10000 || offset == 0x10002) {
        if (!flash_command_high_valid_) {
            flash_command_high_ = value;
            flash_command_high_valid_ = true;
        } else {
            flash_command_high_valid_ = false;
            flash_command((static_cast<u32>(flash_command_high_) << 16) | value);
        }
        return;
    }
    if (flash_mode_ == FlashMode::LoadPage)
        write_bytes(flash_page_.data(), flash_page_.size(), offset & 0x7fU, 2, value);
    else if (flash_mode_ == FlashMode::Status && offset == 0 && value == 0)
        flash_status_ |= 0x0c;
}

void Bus::flash_command(u32 value) {
    flash_open_bus_ = false;
    if (flash_busy_counter_ != 0)
        return;
    const u8 command = static_cast<u8>(value >> 24);
    if (command == 0xd2 && ++flash_status_commands_ < 2)
        return;
    flash_status_commands_ = 0;
    switch (command) {
    case 0x3c:
        flash_erase_ = FlashErase::Chip;
        return;
    case 0x4b:
        flash_erase_ = FlashErase::Sector;
        flash_sector_ = (value & 0x3ffU) >> 7;
        return;
    case 0x78: {
        if (flash_erase_ == FlashErase::None)
            return;
        const std::size_t base = flash_erase_ == FlashErase::Chip ? 0 : flash_sector_ * 0x4000U;
        const std::size_t end =
            flash_erase_ == FlashErase::Chip ? flashram.size() : std::min(base + 0x4000, flashram.size());
        if (base < end)
            std::fill(flashram.begin() + static_cast<std::ptrdiff_t>(base),
                      flashram.begin() + static_cast<std::ptrdiff_t>(end), u8{0xff});
        flash_erase_ = FlashErase::None;
        flash_mode_ = FlashMode::Status;
        flash_status_ = static_cast<u8>((flash_status_ & ~0x80U) | 2U);
        flash_status_stale_ = true;
        flash_busy_counter_ = 5312500; // 85 ms at the 62.5 MHz RCP clock.
        return;
    }
    case 0xa5: {
        const std::size_t base = static_cast<std::size_t>(value & 0x3ffU) << 7;
        for (std::size_t index = 0; index < flash_page_.size() && base + index < flashram.size(); ++index)
            flashram[base + index] &= flash_page_[index];
        flash_page_.fill(0xff);
        flash_mode_ = FlashMode::Status;
        flash_status_ = static_cast<u8>((flash_status_ & ~0x80U) | 1U);
        flash_status_stale_ = true;
        flash_busy_counter_ = 218750; // 3.5 ms at the 62.5 MHz RCP clock.
        return;
    }
    case 0xb4:
        flash_mode_ = FlashMode::LoadPage;
        return;
    case 0xd2:
        flash_mode_ = FlashMode::Status;
        flash_status_stale_ = true;
        return;
    case 0xe1:
        flash_mode_ = FlashMode::SiliconId;
        return;
    case 0xf0:
        flash_mode_ = FlashMode::ReadArray;
        return;
    default:
        return;
    }
}

void Bus::tick_flash(u64 cycles) {
    if (flash_busy_counter_ == 0)
        return;
    if (cycles < flash_busy_counter_) {
        flash_busy_counter_ -= cycles;
        return;
    }
    flash_busy_counter_ = 0;
    flash_status_ = static_cast<u8>((flash_status_ | 0x80U) & ~3U);
}

} // namespace cupid
