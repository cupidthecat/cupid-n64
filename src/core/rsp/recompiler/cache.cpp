#include "core/rsp/recompiler/compiler.hpp"
#include "core/rsp/recompiler/invalidation.hpp"
#include <stdexcept>

namespace cupid::n64 {

RspCompiler::RspCompiler(Rsp &rsp) : impl_(std::make_unique<Impl>(rsp)) {}
RspCompiler::~RspCompiler() = default;

void RspCompiler::reset() {
  impl_->reset();
}

void RspCompiler::recompile() {
  auto replacement = std::make_unique<Impl>(impl_->rsp);
  replacement->dirty = impl_->dirty;
  replacement->external_memory = impl_->external_memory;
  replacement->generation = impl_->generation;
  std::unordered_map<Impl::Block *, Impl::Block *> pointers;
  for (const auto &[key, blocks] : impl_->blocks) {
    auto &rebuilt = replacement->blocks[key];
    for (const auto &block : blocks) {
      auto next = std::make_unique<Impl::Block>();
      next->key = block->key;
      next->words = block->words;
      next->lines = block->lines;
      next->generation = block->generation;
      Emitter emitter(impl_->rsp, *next, next->key.tracing());
      if (!emitter.compile() || next->words != block->words)
        throw std::runtime_error("Signal processor recompilation failed");
      replacement->bytes += next->bytes;
      pointers.emplace(block.get(), next.get());
      rebuilt.push_back(std::move(next));
    }
  }
  for (unsigned mode = 0; mode < impl_->context.size(); ++mode)
    for (unsigned slot = 0; slot < impl_->context[mode].size(); ++slot)
      if (impl_->context[mode][slot])
        replacement->context[mode][slot] = pointers.at(impl_->context[mode][slot]);
  impl_.swap(replacement);
}

void RspCompiler::Impl::reset() {
  for (auto &mode : context)
    mode.fill(nullptr);
  dirty.reset();
  blocks.clear();
  bytes = 0;
  instrumented_bytes = 0;
}

void RspCompiler::Impl::clear_instrumented() {
  for (auto &[key, versions] : blocks) {
    static_cast<void>(key);
    for (auto &block : versions) {
      block->diagnosed.reset();
    }
  }
  instrumented_bytes = 0;
}

void RspCompiler::invalidate(std::uint32_t address, unsigned bytes) {
  ++impl_->generation;
  impl_->dirty |= std::bitset<64>(rsp_cache::line_mask(address, bytes));
}

void RspCompiler::expose_memory() {
  impl_->external_memory = true;
}

void RspCompiler::Impl::clear_dirty() {
  for (auto &mode : context)
    for (auto &entry : mode)
      if (entry && (entry->lines & dirty).any())
        entry = nullptr;
  dirty.reset();
}

bool RspCompiler::Impl::matches(Block &block) {
  if (!external_memory && block.generation == generation)
    return true;
  for (unsigned n = 0; n < block.words.size(); ++n)
    if (block.words[n] != rsp.read_local(0x1000 | ((rsp.pc_ + n * 4) & 0xfff), 4))
      return false;
  block.generation = generation;
  return true;
}

RspCompiler::Impl::Block *RspCompiler::Impl::select() {
#if !SLJIT_64BIT_ARCHITECTURE || SLJIT_CONFIG_UNSUPPORTED
  return nullptr;
#else
  const auto pc = rsp.pc_;
  const auto tracing = rsp.instruction_tracing();
  const Key key{rsp.pipeline_, pc, tracing};
  auto &current = context[tracing][(pc >> 2) & 1023];
  const auto found = blocks.find(key);
  Block *selected = nullptr;
  if (found != blocks.end()) {
    for (const auto &block : found->second) {
      if (matches(*block)) {
        selected = block.get();
        break;
      }
    }
  }
  if (!selected) {
    if (bytes >= 32 * 1024 * 1024)
      reset();
    auto block = std::make_unique<Block>();
    block->key = key;
    Emitter emitter(rsp, *block, tracing);
    if (!emitter.compile())
      return nullptr;
    block->generation = generation;
    block->lines =
        std::bitset<64>(rsp_cache::line_mask(pc, static_cast<unsigned>(block->words.size() * 4)));
    bytes += block->bytes;
    selected = block.get();
    blocks[key].push_back(std::move(block));
  }
  current = selected;
  return selected;
#endif
}

bool RspCompiler::run() {
#if !SLJIT_64BIT_ARCHITECTURE || SLJIT_CONFIG_UNSUPPORTED
  return false;
#else
  auto &cache = *impl_;
  auto &rsp = cache.rsp;
  bool executed = false;
  while (!rsp.status_.halted &&
         (!rsp.diagnostics_ || rsp.diagnostics_->control() == HostControl::None)) {
    if (cache.dirty.any())
      cache.clear_dirty();
    // Retain the selected schedule until its instruction range changes.
    auto *block = cache.context[rsp.instruction_tracing()][(rsp.pc_ >> 2) & 1023];
    if (!block || (cache.external_memory && !cache.matches(*block)))
      block = cache.select();
    if (!block)
      return executed;
    auto *execution = block;
    if (rsp.diagnostic_execution()) {
      auto &variant = block->diagnosed;
      if (variant && variant->homebrew != (rsp.diagnostics_ && rsp.diagnostics_->homebrew())) {
        cache.instrumented_bytes -= variant->bytes;
        variant.reset();
      }
      if (!variant) {
        auto instrumented = std::make_unique<Impl::Block>();
        instrumented->key = block->key;
        instrumented->words = block->words;
        Emitter emitter(rsp, *instrumented, true, true);
        if (!emitter.compile())
          return executed;
        constexpr std::size_t limit = 64 * 1024 * 1024;
        if (instrumented->bytes > limit)
          return executed;
        if (cache.instrumented_bytes > limit - instrumented->bytes)
          cache.clear_instrumented();
        cache.instrumented_bytes += instrumented->bytes;
        variant = std::move(instrumented);
      }
      execution = variant.get();
    }
    const auto before = rsp.clock_;
    execution->execute(rsp);
    rsp.clock_ += rsp.pipeline_.clocks;
    rsp.advance_dma(static_cast<std::uint32_t>(rsp.clock_ - before));
    executed = true;
    if (rsp.clock_ >= 0)
      break;
  }
  return executed;
#endif
}

} // namespace cupid::n64
