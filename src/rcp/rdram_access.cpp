#include "cupid/rdram.hpp"

namespace cupid {

thread_local Rdram::BankAccessScope* Rdram::BankAccessScope::current_ = nullptr;

Rdram::BankAccessScope::BankAccessScope(const Rdram& memory, BankAccessSummary& summary)
    : memory_(memory), summary_(summary), previous_(current_) {
    summary_ = {};
    current_ = this;
    memory_.active_scopes_.fetch_add(1, std::memory_order_relaxed);
}

Rdram::BankAccessScope::~BankAccessScope() {
    memory_.active_scopes_.fetch_sub(1, std::memory_order_relaxed);
    current_ = previous_;
}

void Rdram::merge_bank_accesses(const BankAccessSummary& summary) const {
    for (unsigned index = 0; index < banks_.size(); ++index) {
        const auto& recorded = summary.banks[index];
        if (!recorded.visited)
            continue;
        auto& bank = banks_[index];
        if (!bank.valid || bank.row != recorded.first_row || recorded.changed_row)
            bank.dirty = false;
        bank.dirty |= recorded.dirty;
        bank.valid = true;
        bank.row = recorded.last_row;
        bank.last_access = recorded.last_access;
    }
}

} // namespace cupid
