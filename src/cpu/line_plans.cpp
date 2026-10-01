#include "cupid/cpu.hpp"

namespace cupid {

std::size_t Cpu::CachedLineHash::operator()(const std::array<u8, 32>& image) const {
    u64 hash = 14695981039346656037ULL;
    for (unsigned offset = 0; offset < image.size(); offset += 8U)
        hash = (hash ^ read_be64(image.data() + offset)) * 1099511628211ULL;
    return static_cast<std::size_t>(hash);
}

std::shared_ptr<Cpu::CachedLineContents> Cpu::cached_line_contents(const std::array<u8, 32>& image) {
    if (const auto found = cached_line_contents_.find(image); found != cached_line_contents_.end())
        return found->second;
    // Referenced plans keep their owners when the bounded lookup generation ends.
    if (cached_line_contents_.size() == cached_line_capacity)
        cached_line_contents_.clear();
    auto contents = std::make_shared<CachedLineContents>();
    for (unsigned slot = 0; slot < contents->decoded.size(); ++slot)
        contents->decoded[slot] = decode_cached_instruction(read_be32(image.data() + slot * 4U));
    if (CpuNativeCode::available()) {
        for (unsigned index = 0; index < CpuNativeCode::maximum_instructions; ++index) {
            auto& block = contents->native[index];
            bool stored = false;
            for (unsigned next = index; next < CpuNativeCode::maximum_instructions; ++next) {
                const auto& decoded = contents->decoded[next];
                if (!CpuNativeCode::supports(decoded.word) || (stored && decoded.kind == CachedKind::Load))
                    break;
                stored = stored || decoded.kind == CachedKind::Store;
                ++block.count;
                if (CpuNativeCode::terminal_branch(decoded.word)) {
                    block.ends_branch = true;
                    break;
                }
            }
            unsigned pending_load = 0;
            for (unsigned offset = 0; offset < block.count; ++offset) {
                const auto& decoded = contents->decoded[index + offset];
                const bool issue_wait =
                    pending_load != 0 && (decoded.integer_reads & (1U << pending_load)) != 0;
                block.extra_cycles += static_cast<u8>(issue_wait);
                block.last_cycles = static_cast<u8>(1U + static_cast<unsigned>(issue_wait));
                block.has_load = block.has_load || decoded.kind == CachedKind::Load;
                block.has_store = block.has_store || decoded.kind == CachedKind::Store;
                pending_load = decoded.load_target < 32 ? static_cast<unsigned>(decoded.load_target) : 0U;
            }
            block.pending_load = static_cast<u8>(pending_load);
        }
    }
    cached_line_contents_.emplace(image, contents);
    return contents;
}

} // namespace cupid
