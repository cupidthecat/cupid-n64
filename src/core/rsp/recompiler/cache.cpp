#include "core/rsp/recompiler/compiler.hpp"
#include <algorithm>

namespace cupid::n64 {

RspCompiler::RspCompiler(Rsp &rsp) : impl_(std::make_unique<Impl>(rsp)) {}
RspCompiler::~RspCompiler() = default;

void RspCompiler::reset() {
  impl_->reset();
}

void RspCompiler::Impl::reset() {
  context.fill(nullptr);
  alternate.fill(nullptr);
  dirty.reset();
  blocks.clear();
  bytes = 0;
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
    const auto before = rsp.clock_;
    block->execute(rsp);
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
