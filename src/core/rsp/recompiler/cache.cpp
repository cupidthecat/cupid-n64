#include "core/rsp/recompiler/compiler.hpp"
#include <algorithm>

namespace cupid::n64 {

RspCompiler::RspCompiler(Rsp &rsp) : impl_(std::make_unique<Impl>(rsp)) {}
RspCompiler::~RspCompiler() = default;

void RspCompiler::reset() {
  impl_->context.fill(nullptr);
  impl_->alternate.fill(nullptr);
  impl_->dirty.reset();
  impl_->blocks.clear();
  impl_->bytes = 0;
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

bool RspCompiler::run() {
#if !SLJIT_64BIT_ARCHITECTURE || SLJIT_CONFIG_UNSUPPORTED
  return false;
#else
  auto &rsp = impl_->rsp;
  if (rsp.delay_slot_ || rsp.status_.halted)
    return false;
  const auto execute = [&](const Impl::Block &block) {
    const auto before = rsp.clock_;
    block.execute(rsp);
    rsp.clock_ += rsp.pipeline_.clocks;
    rsp.advance_dma(static_cast<std::uint32_t>(rsp.clock_ - before));
  };
  if (impl_->dirty.any()) {
    for (auto *contexts : {&impl_->context, &impl_->alternate})
      for (auto &entry : *contexts)
        if (entry && (entry->lines & impl_->dirty).any())
          entry = nullptr;
    impl_->dirty.reset();
  }
  const auto pc = rsp.pc_;
  const Impl::Key key{rsp.pipeline_, pc};
  auto &context = impl_->context[(pc >> 2) & 1023];
  auto &alternate = impl_->alternate[(pc >> 2) & 1023];
  const auto matches = [&](Impl::Block &block) {
    if (!impl_->external_memory && block.generation == impl_->generation)
      return true;
    for (unsigned n = 0; n < block.words.size(); ++n)
      if (block.words[n] != rsp.read_local(0x1000 | ((pc + n * 4) & 0xfff), 4))
        return false;
    block.generation = impl_->generation;
    return true;
  };
  if (context && context->key == key && (!impl_->external_memory || matches(*context))) {
    execute(*context);
    return true;
  }
  if (alternate && alternate->key == key && (!impl_->external_memory || matches(*alternate))) {
    std::swap(context, alternate);
    execute(*context);
    return true;
  }
  const auto found = impl_->blocks.find(key);
  Impl::Block *selected = nullptr;
  if (found != impl_->blocks.end()) {
    for (const auto &block : found->second) {
      if (matches(*block)) {
        selected = block.get();
        break;
      }
    }
  }
  if (!selected) {
    if (impl_->bytes >= 32 * 1024 * 1024)
      reset();
    auto block = std::make_unique<Impl::Block>();
    block->key = key;
    Emitter emitter(rsp, *block);
    if (!emitter.compile())
      return false;
    block->generation = impl_->generation;
    for (unsigned n = 0; n < block->words.size(); ++n)
      block->lines.set(((pc + n * 4) & 0xfff) >> 5);
    impl_->bytes += block->bytes;
    selected = block.get();
    impl_->blocks[key].push_back(std::move(block));
  }
  alternate = context;
  context = selected;
  execute(*selected);
  return true;
#endif
}

} // namespace cupid::n64
