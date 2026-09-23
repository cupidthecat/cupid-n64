#include "cupid/rdram.hpp"

#include <algorithm>
#include <bit>

namespace cupid {
namespace {

constexpr u32 chip_size = 2U * 1024 * 1024;
constexpr u32 current_mask = 0x00c0c0c0U;

} // namespace

Rdram::Rdram(std::vector<u8>& bytes) : bytes_(bytes) {
    reset();
}

Rdram::Rdram(const Rdram& other)
    : bytes_(other.bytes_), hidden_(other.hidden_), chips_(other.chips_), banks_(other.banks_),
      clock_(other.clock_), noise_(other.noise_), errors_(other.errors_), active_(other.active_),
      identity_mapping_(other.identity_mapping_),
      active_scopes_(other.active_scopes_.load(std::memory_order_relaxed)) {}

void Rdram::reset(bool warm) {
    active_ = false;
    identity_mapping_ = false;
    errors_ = 0;
    banks_.fill({});
    clock_ = 0;
    if (warm)
        return;
    std::fill(bytes_.begin(), bytes_.end(), u8{0});
    hidden_.assign((bytes_.size() + 1) / 2, 0);
    noise_ = 0x2360ed051fc65da4ULL;
    chips_.fill({});
    for (unsigned index = 0; index < chips_.size(); ++index) {
        auto& chip = chips_[index];
        chip.present = (static_cast<std::size_t>(index) + 1) * chip_size <= bytes_.size();
        chip.registers[0] = 0xb4190010U;
        chip.registers[2] = 0x23;
        chip.registers[9] = 0x500;
        chip.current_low = static_cast<u8>(8 + index);
        chip.current_high = static_cast<u8>(14 + index);
    }
}

void Rdram::set_bus_active(bool active) {
    active_ = active;
    refresh_mapping();
}

void Rdram::refresh_mapping() {
    identity_mapping_ = active_ && !bytes_.empty() && bytes_.size() % chip_size == 0 &&
                        bytes_.size() <= chip_size * chips_.size();
    for (unsigned index = 0; index < chips_.size(); ++index) {
        const auto& chip = chips_[index];
        if (!chip.present)
            continue;
        if (!chip.enabled || chip.device_id != index * 2 || chip.current < chip.current_high) {
            identity_mapping_ = false;
            return;
        }
    }
}

u16 Rdram::decode_device_id(u32 value) {
    const u32 id = ((value >> 26) & 63U) | (((value >> 23) & 1U) << 6) | (((value >> 8) & 255U) << 7) |
                   (((value >> 7) & 1U) << 15);
    return static_cast<u16>(id);
}

u8 Rdram::decode_current(u32 mode) {
    constexpr std::array<unsigned, 6> positions{6, 14, 22, 7, 15, 23};
    unsigned current = 0;
    for (unsigned index = 0; index < positions.size(); ++index) {
        current |= ((mode >> positions[index]) & 1U) << index;
    }
    return static_cast<u8>(current ^ 63U);
}

u32 Rdram::encode_current(u8 current) {
    constexpr std::array<unsigned, 6> positions{6, 14, 22, 7, 15, 23};
    u32 mode = 0;
    for (unsigned index = 0; index < positions.size(); ++index) {
        mode |= ((static_cast<u32>(current ^ 63U) >> index) & 1U) << positions[index];
    }
    return mode;
}

std::optional<unsigned> Rdram::select_chip(u32 address) const {
    if (!active_ || (address & 0x80000U) != 0)
        return std::nullopt;
    const unsigned selector = (address >> 10) & 0x1feU;
    bool saw_disabled = false;
    for (unsigned index = 0; index < chips_.size(); ++index) {
        const auto& chip = chips_[index];
        if (!chip.present)
            continue;
        if (!chip.enabled) {
            if (saw_disabled)
                continue;
            saw_disabled = true;
        }
        if ((chip.device_id & 0xfffeU) == selector)
            return index;
    }
    return std::nullopt;
}

u32 Rdram::read_register(u32 address) const {
    const auto selected = select_chip(address);
    if (!selected)
        return 0;
    const auto& chip = chips_[*selected];
    if ((address & 0x3ffU) >= 0x200U)
        return chip.row;
    if (!chip.enabled)
        return 0;
    const unsigned index = (address >> 2) & 15U;
    if (index >= chip.registers.size())
        return 0;
    if (index == 2)
        return chip.registers[2] | 0x03030203U;
    if (index == 3) {
        const u8 current = chip.auto_current ? chip.latched_current : decode_current(chip.registers[3]);
        return ((chip.registers[3] & ~current_mask) | encode_current(current)) ^ 0xc0c0c0c0U;
    }
    return chip.registers[index];
}

void Rdram::write_chip(Chip& chip, unsigned index, u32 value, unsigned repeat_length) {
    if (index >= chip.registers.size())
        return;
    if (chip.write_delay != 1) {
        if (repeat_length < 16)
            return;
        value = std::rotl(value, 16);
    }
    if (index == 0 || index == 9)
        return;
    chip.registers[index] = value;
    if (index == 1)
        chip.device_id = decode_device_id(value);
    else if (index == 2) {
        chip.registers[2] = value & 0x38381838U;
        chip.write_delay = static_cast<u8>((value >> 3) & 7U);
    } else if (index == 3) {
        chip.current = decode_current(value);
        chip.enabled = (value & 0x02000000U) != 0;
        chip.auto_current = (value & 0x80000000U) != 0;
        if (chip.auto_current)
            chip.latched_current = chip.current;
    }
}

void Rdram::write_register(u32 address, u32 value, unsigned repeat_length) {
    if (!active_)
        return;
    const bool row = (address & 0x3ffU) >= 0x200U;
    const bool broadcast = (address & 0x80000U) != 0;
    const unsigned index = (address >> 2) & 15U;
    if (broadcast) {
        for (auto& chip : chips_) {
            if (!chip.present)
                continue;
            if (row)
                chip.row = value;
            else
                write_chip(chip, index, value, repeat_length);
        }
    } else if (const auto selected = select_chip(address)) {
        auto& chip = chips_[*selected];
        if (row)
            chip.row = value;
        else
            write_chip(chip, index, value, repeat_length);
    }
    refresh_mapping();
}

std::optional<u32> Rdram::translate(u32 address) const {
    if (identity_mapping_) {
        if (address < bytes_.size())
            return address;
        errors_ |= 1U;
        return std::nullopt;
    }
    if (active_) {
        for (unsigned index = 0; index < chips_.size(); ++index) {
            const auto& chip = chips_[index];
            if (chip.present && chip.enabled &&
                static_cast<u32>(chip.device_id >> 1) == address / chip_size) {
                return index * chip_size + address % chip_size;
            }
        }
    }
    errors_ |= 1U;
    return std::nullopt;
}

u64 Rdram::read_reliability(u64 value, unsigned chip_index) const {
    const auto& chip = chips_[chip_index];
    if (chip.current >= chip.current_high)
        return value;
    if (chip.current <= chip.current_low)
        return 0;
    const unsigned threshold = static_cast<unsigned>(chip.current - chip.current_low) * 256 /
                               static_cast<unsigned>(chip.current_high - chip.current_low);
    u64 accepted = 0;
    for (unsigned bit = 0; bit < 64; ++bit) {
        if ((value & (1ULL << bit)) == 0)
            continue;
        noise_ ^= noise_ >> 12;
        noise_ ^= noise_ << 25;
        noise_ ^= noise_ >> 27;
        if (((noise_ * 0x2545f4914f6cdd1dULL) >> 56) < threshold)
            accepted |= 1ULL << bit;
    }
    return accepted;
}

u32 Rdram::read_hidden_word(u32 address) const {
    return (static_cast<u32>(hidden_[address >> 1] & 3U) << 2) | (hidden_[(address >> 1) + 1] & 3U);
}

u64 Rdram::read(u32 address, unsigned width, bool ebus) const {
    if (width != 1 && width != 2 && width != 4 && width != 8)
        return 0;
    address &= ~(width - 1);
    track_access(address, false);
    const auto mapped = translate(address);
    if (!mapped || static_cast<u64>(*mapped) + width > bytes_.size())
        return 0;
    const u32 physical = *mapped;
    if (ebus) {
        if (width == 8)
            return (static_cast<u64>(read_hidden_word(physical)) << 32) | read_hidden_word(physical + 4);
        const u32 word = read_hidden_word(physical & ~3U);
        if (width == 4)
            return word;
        const unsigned shift = (4 - width - (physical & (4 - width))) * 8;
        return (word >> shift) & (width == 1 ? 255U : 65535U);
    }
    const u8* data = bytes_.data() + physical;
    u64 value = 0;
    switch (width) {
    case 1:
        value = data[0];
        break;
    case 2:
        value = read_be16(data);
        break;
    case 4:
        value = read_be32(data);
        break;
    case 8:
        value = read_be64(data);
        break;
    default:
        break;
    }
    return identity_mapping_ ? value : read_reliability(value, physical / chip_size);
}

void Rdram::write_hidden_bit(u32 address, unsigned bit) {
    const unsigned shift = 1 - (address & 1U);
    auto& bits = hidden_[address >> 1];
    bits = static_cast<u8>((bits & ~(1U << shift)) | ((bit & 1U) << shift));
}

void Rdram::write(u32 address, unsigned width, u64 value, bool ebus) {
    if (width != 1 && width != 2 && width != 4 && width != 8)
        return;
    address &= ~(width - 1);
    track_access(address, true);
    const auto mapped = translate(address);
    if (!mapped || static_cast<u64>(*mapped) + width > bytes_.size())
        return;
    const u32 physical = *mapped;
    u8* data = bytes_.data() + physical;
    switch (width) {
    case 1:
        data[0] = static_cast<u8>(value);
        break;
    case 2:
        write_be16(data, static_cast<u16>(value));
        break;
    case 4:
        write_be32(data, static_cast<u32>(value));
        break;
    case 8:
        write_be64(data, value);
        break;
    default:
        break;
    }
    if (ebus) {
        if (width <= 4) {
            for (unsigned byte = 0; byte < width; ++byte) {
                const unsigned position = (physical + byte) & 3U;
                const unsigned bit =
                    position < 4 - width ? 0U : static_cast<unsigned>((value >> (3 - position)) & 1U);
                write_hidden_bit(physical + byte, bit);
            }
        } else {
            for (unsigned byte = 0; byte < 8; ++byte) {
                const unsigned shift = byte < 4 ? 35 - byte : 7 - byte;
                write_hidden_bit(physical + byte, static_cast<unsigned>((value >> shift) & 1U));
            }
        }
    } else if (width == 1) {
        write_hidden_bit(physical, (physical & 1U) != 0 ? static_cast<unsigned>(value & 1U) : 0U);
    } else {
        for (unsigned pair = 0; pair < width / 2; ++pair) {
            hidden_[(physical >> 1) + pair] =
                static_cast<u8>(((value >> ((width / 2 - pair - 1) * 16)) & 1U) * 3);
        }
    }
}

u8 Rdram::hidden_pair(u32 address) const {
    const auto physical = translate(address & ~1U);
    return physical && *physical < bytes_.size() ? hidden_[*physical >> 1] & 3U : 0;
}

void Rdram::set_hidden_pair(u32 address, u8 value) {
    const auto physical = translate(address & ~1U);
    if (physical && *physical < bytes_.size())
        hidden_[*physical >> 1] = value & 3U;
}

} // namespace cupid
