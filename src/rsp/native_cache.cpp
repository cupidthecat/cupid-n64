#include "cupid/rsp/native.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>

namespace cupid {

struct RspNativeCache::Storage {
    struct Key {
        std::array<RspNativeInstruction, 16> instructions{};
        std::size_t count{};
        bool operator==(const Key&) const = default;
    };
    struct Hash {
        std::size_t operator()(const Key& key) const {
            u64 hash = 14695981039346656037ULL ^ key.count;
            for (const auto instruction : std::span(key.instructions).first(key.count)) {
                hash = (hash ^ instruction.word) * 1099511628211ULL;
                hash = (hash ^ static_cast<u64>(instruction.operation)) * 1099511628211ULL;
            }
            return static_cast<std::size_t>(hash);
        }
    };
    struct Entry {
        std::shared_ptr<const RspNativeCode> code;
        unsigned visits{};
        bool complete{};
    };
    std::unordered_map<Key, Entry, Hash> entries;
};

RspNativeCache::RspNativeCache() = default;
RspNativeCache::~RspNativeCache() = default;
// Machine copies can share immutable code in their local blocks. Compilation
// counters and lookup storage belong to the new machine's execution thread.
RspNativeCache::RspNativeCache(const RspNativeCache&) {}
RspNativeCache& RspNativeCache::operator=(const RspNativeCache& other) {
    if (this != &other)
        reset();
    return *this;
}
RspNativeCache::RspNativeCache(RspNativeCache&&) noexcept = default;
RspNativeCache& RspNativeCache::operator=(RspNativeCache&&) noexcept = default;

RspNativeCache::Lookup RspNativeCache::lookup(std::span<const RspNativeInstruction> instructions) {
    if (!RspNativeCode::available() || instructions.empty() || instructions.size() > 16U)
        return {{}, true};
    if (!storage_)
        storage_ = std::make_unique<Storage>();
    Storage::Key key;
    key.count = instructions.size();
    std::copy(instructions.begin(), instructions.end(), key.instructions.begin());
    auto& entries = storage_->entries;
    auto found = entries.find(key);
    if (found == entries.end()) {
        // Existing local blocks retain their code while this lookup generation is
        // discarded. At most 1,024 additional programs can remain in those blocks.
        if (entries.size() == capacity)
            entries.clear();
        found = entries.try_emplace(key).first;
    }
    auto& entry = found->second;
    if (!entry.complete && ++entry.visits >= 5U) {
        entry.code = RspNativeCode::compile(instructions);
        entry.complete = true;
    }
    return {entry.code, entry.complete};
}

std::size_t RspNativeCache::size() const {
    return storage_ ? storage_->entries.size() : 0U;
}

void RspNativeCache::reset() {
    storage_.reset();
}

} // namespace cupid
