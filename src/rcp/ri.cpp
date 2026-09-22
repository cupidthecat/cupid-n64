#include "cupid/system.hpp"

namespace cupid {

u32 Rdram::bank_status() const {
    u32 status = 0;
    for (unsigned index = 0; index < banks_.size(); ++index) {
        const auto& bank = banks_[index];
        status |= static_cast<u32>(bank.valid) << index;
        status |= static_cast<u32>(bank.dirty) << (index + 8);
    }
    return status;
}

void Rdram::invalidate_banks() {
    for (auto& bank : banks_) {
        bank.valid = false;
        bank.dirty = true;
    }
}

bool Rdram::refresh_banks() {
    bool dirty = false;
    for (auto& bank : banks_) {
        dirty |= bank.valid && bank.dirty;
        bank.valid = false;
    }
    return dirty;
}

void Bus::start_rdram_refresh() {
    if ((ri_[4] & 0x20000U) == 0 || !memory.bus_active() || ri_refresh_counter_ != 0)
        return;
    const bool dirty = memory.refresh_banks();
    ri_refresh_counter_ = (ri_[4] >> (dirty ? 8 : 0)) & 0xffU;
}

void Rdram::track_access(u32 address, bool write) const {
    if (address >= 0x00800000U) {
        if (address < 0x03f00000U)
            errors_ |= 4U;
        return;
    }
    if (!active_)
        return;
    const u16 row = static_cast<u16>((address >> 11) & 0x1ffU);
    if (auto* scope = BankAccessScope::current_; scope != nullptr && &scope->memory_ == this) {
        auto& bank = scope->summary_.banks[address >> 20];
        if (!bank.visited) {
            bank.visited = true;
            bank.first_row = row;
            bank.last_row = row;
        } else if (bank.last_row != row) {
            bank.last_row = row;
            bank.changed_row = true;
            bank.dirty = false;
        }
        bank.dirty |= write;
        bank.last_access = clock_;
        return;
    }
    auto& bank = banks_[address >> 20];
    if (!bank.valid || bank.row != row) {
        bank.row = row;
        bank.valid = true;
        bank.dirty = false;
    }
    bank.dirty |= write;
    bank.last_access = clock_;
}

bool Rdram::row_open(u32 address) const {
    if (address >= 0x00800000U || !active_)
        return true;
    const auto& bank = banks_[address >> 20];
    return bank.valid && bank.row == static_cast<u16>((address >> 11) & 0x1ffU);
}

u64 Rdram::bank_access_clock(u32 address) const {
    if (address >= 0x00800000U)
        return clock_;
    return banks_[address >> 20].last_access;
}

u32 Bus::read_ri(u32 offset) const {
    const unsigned index = (offset & 0x1fU) >> 2;
    if (index == 2)
        return (memory.errors() & 1U) | 6U | (ri_[0] & 8U) | (ri_[3] & 0x10U);
    if (index == 6)
        return memory.errors();
    if (index == 7)
        return memory.bank_status();
    return ri_[index];
}

void Bus::write_ri(u32 offset, u32 value) {
    const unsigned index = (offset & 0x1fU) >> 2;
    if (index == 6) {
        memory.clear_error();
    } else if (index == 7) {
        memory.invalidate_banks();
    } else {
        ri_[index] = value;
        if (index == 2)
            ri_current_loaded_ = true;
        if (index == 2 || index == 3)
            memory.set_bus_active(ri_current_loaded_ && ri_[3] == 0x14);
    }
}

} // namespace cupid
