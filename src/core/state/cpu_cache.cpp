#include "core/cpu/recompiler/compiler.hpp"
#include "core/state/archive.hpp"
#include "core/state/core_state.hpp"
#include <algorithm>

namespace cupid::n64 {

void CoreState::visit(state::Archive &a, InstructionTracker &tracker) {
  a.identity(static_cast<std::uint32_t>(tracker.sections_.size()));
  for (auto &section : tracker.sections_) {
    a.field(section.generation);
    std::bitset<128> lines;
    for (unsigned word = 0; word < 2; ++word) {
      std::uint64_t bits = 0;
      if (!a.loading())
        for (unsigned bit = 0; bit < 64; ++bit)
          bits |= std::uint64_t(section.lines[word * 64 + bit]) << bit;
      bits = a.literal(bits);
      for (unsigned bit = 0; bit < 64; ++bit)
        lines[word * 64 + bit] = (bits >> bit) & 1;
    }
    a.defer([&section, lines] { section.lines = lines; });
  }
  const auto fully_tracked = a.literal(tracker.fully_tracked_);
  state::Archive::require(!a.loading() || !fully_tracked || tracker.fully_tracked_);
  a.defer([&tracker, fully_tracked] { tracker.fully_tracked_ = fully_tracked; });
}

void CoreState::visit(state::Archive &a, CpuCompiler &compiler, InstructionTracker &tracker) {
  using Cache = CpuCompiler::Impl;
  using Block = Cache::Block;
  auto &cache = *compiler.impl_;
  state::Archive::require(!cache.active.tracker);
  constexpr unsigned allowed_mode = 3 | 0x24000000 | (0x00000f83 << 2) | 0x01000000;
  const auto valid_key = [](const Cache::Key &key) {
    state::Archive::require(!(key.pc & 3) && !(key.mode & ~allowed_mode));
  };
  const auto valid_words = [](const Block &block) {
    state::Archive::require(!block.words.empty());
    state::Archive::require((block.physical & 4095) + block.words.size() * 4 <= 4096);
    bool delay = false;
    for (unsigned n = 0; n < block.words.size(); ++n) {
      const auto info = block_instruction(block.words[n]);
      const bool terminal = delay || (!info.branch && info.terminal);
      state::Archive::require(!terminal || n + 1 == block.words.size());
      if (n + 1 == block.words.size())
        state::Archive::require(terminal ||
                                (block.physical & 4095) + block.words.size() * 4 == 4096);
      delay = info.stop_after_delay;
    }
  };
  std::vector<std::uint32_t> pages;
  if (!a.loading()) {
    for (unsigned n = 0; n < cache.sections.size(); ++n)
      if (cache.sections[n])
        pages.push_back(n << 12);
    for (const auto &[page, section] : cache.other_sections) {
      static_cast<void>(section);
      pages.push_back(page);
    }
    std::sort(pages.begin(), pages.end());
  }
  const auto count = a.literal(static_cast<std::uint32_t>(pages.size()));
  constexpr unsigned physical_page_count = 1u << 20;
  state::Archive::require(count <= physical_page_count);
  auto restored = std::make_shared<Cache>(cache.cpu);
  std::uint32_t previous_page = 0;
  for (unsigned n = 0; n < count; ++n) {
    const auto page = a.literal(a.loading() ? 0u : pages[n]);
    state::Archive::require(!(page & 4095) && (!n || page > previous_page));
    previous_page = page;
    auto &section = a.loading() ? restored->section(page) : cache.section(page);
    const auto tracked = a.literal(section.tracker != nullptr);
    state::Archive::require(!section.tracker || section.tracker == &tracker);
    section.generation = a.literal(section.generation);
    if (a.loading())
      section.tracker = tracked ? &tracker : nullptr;
    std::vector<Block *> blocks;
    std::vector<std::pair<unsigned, Cache::Entry *>> entries;
    if (!a.loading())
      for (unsigned slot = 0; slot < section.entries.size(); ++slot)
        for (auto *entry = section.entries[slot].get(); entry; entry = entry->next.get()) {
          entries.emplace_back(slot, entry);
          if (std::find(blocks.begin(), blocks.end(), entry->block.get()) == blocks.end())
            blocks.push_back(entry->block.get());
        }
    const auto block_count = a.literal(static_cast<std::uint32_t>(blocks.size()));
    state::Archive::require(block_count <= 65536);
    std::vector<std::shared_ptr<Block>> decoded_blocks;
    for (unsigned index = 0; index < block_count; ++index) {
      auto decoded = a.loading() ? std::make_shared<Block>() : nullptr;
      auto &block = a.loading() ? *decoded : *blocks[index];
      block.key.pc = a.literal(block.key.pc);
      block.key.mode = a.literal(block.key.mode);
      block.physical = a.literal(block.physical);
      valid_key(block.key);
      state::Archive::require((block.physical & ~4095u) == page);
      state::Archive::require((block.physical & 4095) == (block.key.pc & 4095));
      const auto bytes = a.literal(static_cast<std::uint64_t>(block.bytes));
      state::Archive::require(bytes && bytes <= 4 * 1024 * 1024);
      auto words = a.owned_vector(block.words, 1024);
      auto aliases = a.owned_vector(block.entries, 1024);
      if (a.loading()) {
        block.bytes = static_cast<std::size_t>(bytes);
        block.words = std::move(words);
        block.entries = std::move(aliases);
        decoded_blocks.push_back(std::move(decoded));
        blocks.push_back(&block);
      }
      valid_words(block);
      if (a.loading()) {
        const auto expected_entries = std::move(block.entries);
        block.entries.clear();
        CpuCompiler::Emitter plan(cache.cpu, block, block.key.pc, block.physical,
                                  (block.key.mode & 2) != 0);
        plan.plan_entries();
        state::Archive::require(block.entries == expected_entries);
      }
      unsigned previous = 0;
      for (auto alias : block.entries) {
        state::Archive::require(alias > previous && alias < block.words.size());
        state::Archive::require(!block_instruction(block.words[alias - 1]).branch);
        previous = alias;
      }
      if (a.loading()) {
        section.bytes += block.bytes;
        state::Archive::require(restored->bytes + section.bytes <= 67 * 1024 * 1024);
      }
    }
    const auto entry_count = a.literal(static_cast<std::uint32_t>(entries.size()));
    state::Archive::require(entry_count <= 1048576);
    std::array<Cache::Entry *, 1024> tails{};
    std::vector<bool> referenced(block_count);
    unsigned previous_slot = 0;
    for (unsigned index = 0; index < entry_count; ++index) {
      auto decoded = a.loading() ? std::make_unique<Cache::Entry>() : nullptr;
      auto &entry = a.loading() ? *decoded : *entries[index].second;
      const auto slot = a.literal(a.loading() ? 0u : entries[index].first);
      state::Archive::require(slot < 1024 && (!index || slot >= previous_slot));
      previous_slot = slot;
      entry.key.pc = a.literal(entry.key.pc);
      entry.key.mode = a.literal(entry.key.mode);
      valid_key(entry.key);
      state::Archive::require(((entry.key.pc >> 2) & 1023) == slot);
      std::uint32_t owner = 0;
      if (!a.loading()) {
        const auto found = std::find(blocks.begin(), blocks.end(), entry.block.get());
        state::Archive::require(found != blocks.end());
        owner = static_cast<std::uint32_t>(found - blocks.begin());
      }
      owner = a.literal(owner);
      state::Archive::require(owner < block_count);
      referenced[owner] = true;
      entry.index = a.literal(entry.index);
      const auto &block = *blocks[owner];
      state::Archive::require(entry.index < block.words.size());
      state::Archive::require(entry.key.pc == block.key.pc + entry.index * 4 &&
                              entry.key.mode == block.key.mode);
      state::Archive::require(!entry.index || std::binary_search(block.entries.begin(),
                                                                 block.entries.end(), entry.index));
      entry.cache_generation = a.literal(entry.cache_generation);
      entry.tracked = a.literal(entry.tracked);
      if (a.loading()) {
        state::Archive::require(!section.find(entry.key));
        entry.block = decoded_blocks[owner];
        auto *tail = decoded.get();
        if (tails[slot])
          tails[slot]->next = std::move(decoded);
        else
          section.entries[slot] = std::move(decoded);
        tails[slot] = tail;
      }
    }
    state::Archive::require(
        std::all_of(referenced.begin(), referenced.end(), [](bool value) { return value; }));
    if (a.loading()) {
      restored->bytes += section.bytes;
      state::Archive::require(restored->bytes <= 67 * 1024 * 1024);
    }
  }
  if (a.loading())
    a.defer([&cache, restored] {
      cache.sections.swap(restored->sections);
      cache.other_sections.swap(restored->other_sections);
      cache.bytes = restored->bytes;
      cache.active = {};
    });
}

} // namespace cupid::n64
