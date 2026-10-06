#include "core/rsp/recompiler/compiler.hpp"
#include <algorithm>

namespace cupid::n64 {

RspCompiler::RspCompiler(Rsp &rsp) : impl_(std::make_unique<Impl>(rsp)) {}
RspCompiler::~RspCompiler() = default;

void RspCompiler::reset() {
  impl_->context.fill(nullptr);
  impl_->dirty.reset();
  impl_->blocks.clear();
  impl_->bytes = 0;
}

void RspCompiler::invalidate(std::uint32_t address, unsigned bytes) {
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
  if (rsp.delay_slot_ || rsp.dma_busy() || rsp.status_.halted)
    return false;
  if (impl_->dirty.any()) {
    for (auto &entry : impl_->context)
      if (entry && (entry->lines & impl_->dirty).any())
        entry = nullptr;
    impl_->dirty.reset();
  }
  const Impl::Key key{rsp.pipeline_.previous, rsp.pc_, rsp.pipeline_.single_issue};
  auto &context = impl_->context[(key.pc >> 2) & 1023];
  const auto matches = [&](const Impl::Block &block) {
    for (unsigned n = 0; n < block.words.size(); ++n)
      if (block.words[n] != rsp.read_local(0x1000 | ((key.pc + n * 4) & 0xfff), 4))
        return false;
    return true;
  };
  if (context && context->key == key && (!impl_->external_memory || matches(*context))) {
    context->execute();
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
    for (unsigned n = 0; n < block->words.size(); ++n)
      block->lines.set(((key.pc + n * 4) & 0xfff) >> 5);
    impl_->bytes += block->bytes;
    selected = block.get();
    impl_->blocks[key].push_back(std::move(block));
  }
  context = selected;
  selected->execute();
  return true;
#endif
}

} // namespace cupid::n64
