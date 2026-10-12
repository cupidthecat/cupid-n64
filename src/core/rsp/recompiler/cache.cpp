#include "core/rsp/recompiler/compiler.hpp"
#include <algorithm>
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
      Emitter emitter(impl_->rsp, *next);
      if (!emitter.compile() || next->words != block->words)
        throw std::runtime_error("Signal processor recompilation failed");
      replacement->bytes += next->bytes;
      pointers.emplace(block.get(), next.get());
      rebuilt.push_back(std::move(next));
    }
  }
  for (unsigned n = 0; n < impl_->context.size(); ++n) {
    if (impl_->context[n])
      replacement->context[n] = pointers.at(impl_->context[n]);
    if (impl_->alternate[n])
      replacement->alternate[n] = pointers.at(impl_->alternate[n]);
  }
  impl_.swap(replacement);
}

void RspCompiler::Impl::reset() {
  context.fill(nullptr);
  alternate.fill(nullptr);
  dirty.reset();
  blocks.clear();
  bytes = 0;
  trace_bytes = 0;
}

void RspCompiler::Impl::clear_traced() {
  for (auto &[key, versions] : blocks) {
    static_cast<void>(key);
    for (auto &block : versions)
      block->traced.reset();
  }
  trace_bytes = 0;
}

void RspCompiler::invalidate(std::uint32_t address, unsigned bytes) {
  ++impl_->generation;
  if (bytes >= 4096) {
    impl_->dirty.set();
    return;
  }
  while (bytes) {
    impl_->dirty.set((address & 0xfff) >> 5);
    const auto count = std::min(bytes, 32 - (address & 31));
    address += count;
    bytes -= count;
  }
}

void RspCompiler::expose_memory() {
  impl_->external_memory = true;
}

void RspCompiler::Impl::clear_dirty() {
  for (auto *contexts : {&context, &alternate})
    for (auto &entry : *contexts)
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
  const Key key{rsp.pipeline_, pc};
  auto &current = context[(pc >> 2) & 1023];
  auto &other = alternate[(pc >> 2) & 1023];
  if (other && other->key == key && (!external_memory || matches(*other))) {
    std::swap(current, other);
    return current;
  }
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
    Emitter emitter(rsp, *block);
    if (!emitter.compile())
      return nullptr;
    block->generation = generation;
    for (unsigned n = 0; n < block->words.size(); ++n)
      block->lines.set(((pc + n * 4) & 0xfff) >> 5);
    bytes += block->bytes;
    selected = block.get();
    blocks[key].push_back(std::move(block));
  }
  other = current;
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
  while (!rsp.status_.halted) {
    if (cache.dirty.any())
      cache.clear_dirty();
    // Retain the selected schedule until its instruction range changes.
    auto *block = cache.context[(rsp.pc_ >> 2) & 1023];
    if (!block || (cache.external_memory && !cache.matches(*block)))
      block = cache.select();
    if (!block)
      return executed;
    auto *execution = block;
    if (rsp.instruction_tracing()) {
      if (!block->traced) {
        auto traced = std::make_unique<Impl::Block>();
        traced->key = block->key;
        traced->words = block->words;
        Emitter emitter(rsp, *traced, true);
        if (!emitter.compile())
          return executed;
        constexpr std::size_t limit = 64 * 1024 * 1024;
        if (traced->bytes > limit)
          return executed;
        if (cache.trace_bytes > limit - traced->bytes)
          cache.clear_traced();
        cache.trace_bytes += traced->bytes;
        block->traced = std::move(traced);
      }
      execution = block->traced.get();
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
