#include "core/rsp/recompiler/compiler.hpp"
#include "core/state/archive.hpp"
#include "core/state/core_state.hpp"
#include <algorithm>
#include <tuple>

namespace cupid::n64 {

void CoreState::visit(state::Archive &a, RspCompiler &compiler) {
  using Cache = RspCompiler::Impl;
  using Block = Cache::Block;
  constexpr std::uint64_t maximum_cache_bytes = 36ull * 1024 * 1024;
  auto &cache = *compiler.impl_;
  const auto pipeline = [&](Rsp::Pipeline &p) {
    for (auto &stage : p.previous) {
      stage.gpr = a.literal(stage.gpr);
      stage.vector = a.literal(stage.vector);
      stage.load = a.literal(stage.load);
    }
    auto &op = p.current;
    op.flags = a.literal(op.flags);
    op.read_gpr = a.literal(op.read_gpr);
    op.write_gpr = a.literal(op.write_gpr);
    op.read_vector = a.literal(op.read_vector);
    op.write_vector = a.literal(op.write_vector);
    op.read_control = a.literal(op.read_control);
    op.write_control = a.literal(op.write_control);
    op.fake_vector = a.literal(op.fake_vector);
    p.clocks = a.literal(p.clocks);
    p.single_issue = a.literal(p.single_issue);
    state::Archive::require(p.clocks == 0);
  };
  const auto same_pipeline = [](const Rsp::Pipeline &left, const Rsp::Pipeline &right) {
    if (left.clocks != right.clocks || left.single_issue != right.single_issue)
      return false;
    for (unsigned n = 0; n < left.previous.size(); ++n)
      if (left.previous[n].gpr != right.previous[n].gpr ||
          left.previous[n].vector != right.previous[n].vector ||
          left.previous[n].load != right.previous[n].load)
        return false;
    const auto &x = left.current;
    const auto &y = right.current;
    return std::tie(x.flags, x.read_gpr, x.write_gpr, x.read_vector, x.write_vector, x.read_control,
                    x.write_control, x.fake_vector) ==
           std::tie(y.flags, y.read_gpr, y.write_gpr, y.read_vector, y.write_vector, y.read_control,
                    y.write_control, y.fake_vector);
  };
  const auto generation = a.literal(cache.generation);
  const auto external = a.literal(cache.external_memory);
  state::Archive::require(!a.loading() || external || !cache.external_memory);
  std::bitset<128> dirty;
  for (unsigned word = 0; word < 2; ++word) {
    std::uint64_t bits = 0;
    if (!a.loading())
      for (unsigned bit = 0; bit < 64; ++bit)
        bits |= std::uint64_t(cache.dirty[word * 64 + bit]) << bit;
    bits = a.literal(bits);
    for (unsigned bit = 0; bit < 64; ++bit)
      dirty[word * 64 + bit] = (bits >> bit) & 1;
  }
  std::vector<Block *> ordered;
  if (!a.loading()) {
    std::vector<Cache::Key> keys;
    for (const auto &[key, blocks] : cache.blocks) {
      static_cast<void>(blocks);
      keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end(), [](const auto &x, const auto &y) {
      return std::tie(x.pc, x.flags, x.registers) < std::tie(y.pc, y.flags, y.registers);
    });
    for (const auto &key : keys)
      for (const auto &block : cache.blocks.at(key))
        ordered.push_back(block.get());
  }
  const auto count = a.literal(static_cast<std::uint32_t>(ordered.size()));
  state::Archive::require(count <= maximum_cache_bytes);
  auto restored = std::make_shared<Cache>(cache.rsp);
  for (unsigned n = 0; n < count; ++n) {
    std::unique_ptr<Block> decoded;
    Block *block = nullptr;
    if (a.loading()) {
      decoded = std::make_unique<Block>();
      block = decoded.get();
    } else {
      block = ordered[n];
    }
    block->key.pc = a.literal(block->key.pc);
    block->key.flags = a.literal(block->key.flags);
    state::Archive::require(block->key.pc <= 0xfff && !(block->key.pc & 3));
    state::Archive::require(block->key.flags <= 15);
    for (auto &registers : block->key.registers)
      registers = a.literal(registers);
    block->generation = a.literal(block->generation);
    auto expected = block->pipeline;
    pipeline(expected);
    auto words = a.owned_vector(block->words, 1024);
    state::Archive::require(!words.empty());
    if (a.loading()) {
      block->words = std::move(words);
      const auto original_words = block->words;
      RspCompiler::Emitter emitter(cache.rsp, *block);
      state::Archive::require(emitter.compile());
      state::Archive::require(block->words == original_words);
      state::Archive::require(same_pipeline(block->pipeline, expected));
      for (unsigned word = 0; word < block->words.size(); ++word)
        block->lines.set(((block->key.pc + word * 4) & 0xfff) >> 5);
      restored->bytes += block->bytes;
      state::Archive::require(restored->bytes <= maximum_cache_bytes);
      ordered.push_back(block);
      restored->blocks[block->key].push_back(std::move(decoded));
    }
  }
  for (auto *contexts : {&cache.context, &cache.alternate})
    for (unsigned slot = 0; slot < contexts->size(); ++slot) {
      std::uint32_t index = 0;
      if (!a.loading() && (*contexts)[slot]) {
        const auto found = std::find(ordered.begin(), ordered.end(), (*contexts)[slot]);
        state::Archive::require(found != ordered.end());
        index = static_cast<std::uint32_t>(found - ordered.begin()) + 1;
      }
      index = a.literal(index);
      state::Archive::require(index <= count);
      auto *block = index ? ordered[index - 1] : nullptr;
      state::Archive::require(!block || ((block->key.pc >> 2) & 1023) == slot);
      if (a.loading()) {
        auto &target = contexts == &cache.context ? restored->context : restored->alternate;
        target[slot] = block;
      }
    }
  if (a.loading()) {
    restored->generation = generation;
    restored->external_memory = external;
    restored->dirty = dirty;
    a.defer([&cache, restored] {
      cache.blocks.swap(restored->blocks);
      cache.context = restored->context;
      cache.alternate = restored->alternate;
      cache.bytes = restored->bytes;
      cache.generation = restored->generation;
      cache.external_memory = restored->external_memory;
      cache.dirty = restored->dirty;
    });
  }
}

} // namespace cupid::n64
