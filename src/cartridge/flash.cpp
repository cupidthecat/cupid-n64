#include "cupid/bus.hpp"

#include <algorithm>

namespace cupid {
namespace {

struct FlashParameters {
    u16 manufacturer;
    u16 device;
    bool halfword_indexed;
    u64 program_cycles;
    u64 sector_cycles;
    u64 chip_cycles;
};

const FlashParameters& parameters(FlashChip chip) {
    static constexpr std::array<FlashParameters, 7> chips{{
        {0x00c2, 0x0000, true, 218750, 5312500, 5312500},
        {0x00c2, 0x0001, true, 218750, 5312500, 5312500},
        {0x00c2, 0x001e, true, 218750, 5312500, 5312500},
        {0x00c2, 0x001d, false, 218750, 5312500, 5312500},
        {0x00c2, 0x0084, false, 218750, 5312500, 5312500},
        {0x00c2, 0x008e, false, 218750, 5312500, 5312500},
        {0x0032, 0x00f1, false, 18750, 17500000, 18750000},
    }};
    const auto index = static_cast<unsigned>(chip);
    return chips[index < chips.size() ? index : 2];
}

bool macronix(FlashChip chip) {
    return parameters(chip).manufacturer == 0x00c2;
}

} // namespace

void Bus::set_flash_chip(FlashChip chip) {
    flash_chip_ = chip;
    reset_flash();
}

void Bus::reset_flash() {
    flash_busy_counter_ = 0;
    flash_mode_ = FlashMode::ReadArray;
    flash_erase_ = FlashErase::None;
    flash_sector_ = 0;
    flash_page_.fill(0xff);
    flash_status_ = macronix(flash_chip_) ? 0x8c : 0x80;
    flash_status_commands_ = 0;
    flash_command_high_ = 0;
    flash_previous_read_ = 0;
    flash_burst_index_ = 0;
    flash_command_high_valid_ = false;
    flash_status_stale_ = false;
    flash_open_bus_ = false;
}

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
        if (!macronix(flash_chip_) && (cart_offset_ & 0x20000U) != 0) {
            flash_open_bus_ = true;
            return std::nullopt;
        }
        const auto& chip = parameters(flash_chip_);
        const std::array<u16, 4> id{0x1111, 0x8001, chip.manufacturer, chip.device};
        value = !macronix(flash_chip_) && flash_burst_index_ >= id.size()
                    ? chip.device
                    : id[flash_burst_index_ % id.size()];
        break;
    }
    case FlashMode::LoadPage:
        value = static_cast<u16>(read_bytes(flash_page_.data(), flash_page_.size(), cart_offset_ & 0x7eU, 2));
        break;
    case FlashMode::ReadArray: {
        const bool indexed = parameters(flash_chip_).halfword_indexed;
        const u32 address = indexed ? cart_offset_ << 1 : cart_offset_;
        value = static_cast<u16>(read_bytes(flashram.data(), flashram.size(), address, 2));
        const u32 mask = indexed ? 0x3fffU : 0x7fffU;
        const u32 increment = indexed ? 1U : 2U;
        cart_offset_ = (cart_offset_ & ~mask) | ((cart_offset_ + increment) & mask);
        flash_previous_read_ = value;
        return value;
    }
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
    if (flash_mode_ == FlashMode::LoadPage) {
        const u32 index = offset & 0x7eU;
        if (macronix(flash_chip_)) {
            write_bytes(flash_page_.data(), flash_page_.size(), index, 2, value);
        } else {
            flash_page_[index] &= static_cast<u8>(value >> 8);
            flash_page_[index + 1] &= static_cast<u8>(value);
        }
    } else if (flash_mode_ == FlashMode::Status) {
        if (!macronix(flash_chip_))
            flash_status_ &= static_cast<u8>(~0x0cU);
        else if (offset == 0 && value == 0)
            flash_status_ |= 0x0c;
    }
}

void Bus::flash_command(u32 value) {
    flash_open_bus_ = false;
    if (flash_busy_counter_ != 0)
        return;
    const u8 command = static_cast<u8>(value >> 24);
    if (macronix(flash_chip_) && command == 0xd2 && ++flash_status_commands_ < 2)
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
        const bool whole_chip = flash_erase_ == FlashErase::Chip;
        const std::size_t base = whole_chip ? 0 : flash_sector_ * 0x4000U;
        const std::size_t end =
            flash_erase_ == FlashErase::Chip ? flashram.size() : std::min(base + 0x4000, flashram.size());
        if (base < end)
            std::fill(flashram.begin() + static_cast<std::ptrdiff_t>(base),
                      flashram.begin() + static_cast<std::ptrdiff_t>(end), u8{0xff});
        flash_erase_ = FlashErase::None;
        flash_mode_ = FlashMode::Status;
        flash_status_ = static_cast<u8>((flash_status_ & ~0x80U) | 2U);
        flash_status_stale_ = macronix(flash_chip_);
        const auto& chip = parameters(flash_chip_);
        flash_busy_counter_ = whole_chip ? chip.chip_cycles : chip.sector_cycles;
        return;
    }
    case 0xa5: {
        const std::size_t base = static_cast<std::size_t>(value & 0x3ffU) << 7;
        for (std::size_t index = 0; index < flash_page_.size() && base + index < flashram.size(); ++index)
            flashram[base + index] &= flash_page_[index];
        flash_page_.fill(0xff);
        flash_mode_ = FlashMode::Status;
        flash_status_ = static_cast<u8>((flash_status_ & ~0x80U) | 1U);
        flash_status_stale_ = macronix(flash_chip_);
        flash_busy_counter_ = parameters(flash_chip_).program_cycles;
        return;
    }
    case 0xb4:
        flash_mode_ = FlashMode::LoadPage;
        return;
    case 0xd2:
        flash_mode_ = FlashMode::Status;
        flash_status_stale_ = macronix(flash_chip_);
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
    if (!macronix(flash_chip_))
        flash_status_ |= static_cast<u8>((flash_status_ & 3U) << 2);
    flash_status_ = static_cast<u8>((flash_status_ | 0x80U) & ~3U);
}

} // namespace cupid
