#include "cupid/rcp/transfer_pak.hpp"

#include <utility>

namespace cupid {

void TransferPak::insert(GameBoyCartridge cartridge) {
    cartridge.power();
    cartridge_ = std::move(cartridge);
}

void TransferPak::remove() {
    cartridge_.reset();
}

void TransferPak::disconnect() {
    enabled_ = access_ = false;
    bank_ = reset_ = 0;
    if (cartridge_)
        cartridge_->power();
}

u8 TransferPak::read(u16 address) {
    address &= 0x7fff;
    if (!enabled_)
        return 0;
    if (address < 0x2000)
        return 0x84;
    if (address < 0x3000)
        return bank_;
    if (address < 0x4000) {
        const auto status = static_cast<u8>(0x80U | (cartridge_ ? 0U : 0x40U) |
                                            (static_cast<unsigned>(reset_) << 2U) | (access_ ? 1U : 0U));
        if (access_ && reset_ == 3)
            reset_ = 2;
        else if (!access_ && (reset_ == 1 || reset_ == 2))
            --reset_;
        return status;
    }
    if (!access_ || !cartridge_)
        return 0;
    return cartridge_->read(static_cast<u16>((static_cast<unsigned>(bank_) << 14U) | (address & 0x3fffU)));
}

void TransferPak::write(u16 address, u8 value) {
    address &= 0x7fff;
    if (address < 0x2000) {
        if (value == 0x84 && !enabled_) {
            enabled_ = true;
            access_ = false;
            bank_ = 3;
            reset_ = 0;
        } else if (value == 0xfe) {
            enabled_ = false;
            if (cartridge_)
                cartridge_->power();
        }
        return;
    }
    if (!enabled_)
        return;
    if (address < 0x3000) {
        bank_ = value <= 3 ? value : 0;
    } else if (address < 0x4000) {
        const bool next = (value & 1U) != 0;
        if (next && !access_) {
            reset_ = 3;
            if (cartridge_)
                cartridge_->power();
        }
        access_ = next;
    } else if (access_ && cartridge_) {
        cartridge_->write(static_cast<u16>((static_cast<unsigned>(bank_) << 14U) | (address & 0x3fffU)),
                          value);
    }
}

} // namespace cupid
