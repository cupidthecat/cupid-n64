#include "cupid/cpu/native_cache.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>

namespace cupid {

struct CpuNativeCache::Storage {
    struct Key {
        std::array<u32, CpuNativeCode::maximum_instructions> instructions{};
        std::size_t count{};
        bool operator==(const Key&) const = default;
    };
    struct Hash {
        std::size_t operator()(const Key& key) const {
            u64 hash = 14695981039346656037ULL ^ key.count;
            for (const u32 word : std::span(key.instructions).first(key.count))
                hash = (hash ^ word) * 1099511628211ULL;
            return static_cast<std::size_t>(hash);
        }
    };
    std::unordered_map<Key, std::shared_ptr<const CpuNativeCode>, Hash> entries;
};

CpuNativeCache::CpuNativeCache() = default;
CpuNativeCache::~CpuNativeCache() = default;
CpuNativeCache::CpuNativeCache(const CpuNativeCache&) {}
CpuNativeCache& CpuNativeCache::operator=(const CpuNativeCache& other) {
    if (this != &other)
        reset();
    return *this;
}
CpuNativeCache::CpuNativeCache(CpuNativeCache&&) noexcept = default;
CpuNativeCache& CpuNativeCache::operator=(CpuNativeCache&&) noexcept = default;

std::shared_ptr<const CpuNativeCode> CpuNativeCache::lookup(std::span<const u32> instructions) {
    if (!CpuNativeCode::available() || instructions.empty() ||
        instructions.size() > CpuNativeCode::maximum_instructions)
        return {};
    if (!storage_)
        storage_ = std::make_unique<Storage>();
    Storage::Key key;
    key.count = instructions.size();
    std::copy(instructions.begin(), instructions.end(), key.instructions.begin());
    auto& entries = storage_->entries;
    if (const auto found = entries.find(key); found != entries.end())
        return found->second;
    // Hardware cache replacement changes the local plan, not an immutable program.
    // Local plans retain their owners when the bounded lookup generation is cleared.
    if (entries.size() == capacity)
        entries.clear();
    auto code = CpuNativeCode::compile(instructions);
    entries.emplace(key, code);
    return code;
}

std::size_t CpuNativeCache::size() const {
    return storage_ ? storage_->entries.size() : 0U;
}

void CpuNativeCache::reset() {
    storage_.reset();
}

} // namespace cupid
