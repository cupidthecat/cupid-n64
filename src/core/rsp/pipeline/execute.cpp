#include "core/rsp/recompiler.hpp"
#include "core/rsp/rsp.hpp"
#include <algorithm>

namespace cupid::n64 {

void Rsp::Pipeline::issue(const OpInfo &op) {
  current.read_gpr |= op.read_gpr;
  if (!(op.flags & Bypass))
    current.write_gpr |= op.write_gpr & ~1u;
  current.read_vector |= op.read_vector;
  current.write_vector |= op.write_vector;
  current.flags |= op.flags & (Load | Store | Branch);
}

void Rsp::Pipeline::stall() {
  previous[2] = previous[1];
  previous[1] = previous[0];
  previous[0] = {};
  clocks += 3;
}

void Rsp::Pipeline::end() {
  if (current.read_gpr & previous[0].gpr) {
    stall();
    stall();
  } else if (current.read_gpr & previous[1].gpr)
    stall();
  if (current.read_vector & previous[0].vector) {
    stall();
    stall();
    stall();
  } else if (current.read_vector & previous[1].vector) {
    stall();
    stall();
  } else if (current.read_vector & previous[2].vector)
    stall();
  if (current.flags & Store) {
    while (previous[1].load)
      stall();
  }
  single_issue = current.flags & Branch;
  previous[2] = previous[1];
  previous[1] = previous[0];
  previous[0] = {current.write_gpr, current.write_vector, bool(current.flags & Load)};
  current = {};
  clocks += 3;
}

bool Rsp::dual_issue(const OpInfo &first, const OpInfo &second) {
  return bool(first.flags & Vector) != bool(second.flags & Vector) &&
         !(first.write_vector & (second.read_vector | second.write_vector)) &&
         !(first.write_control & (second.read_control | second.write_control)) &&
         !(((first.flags | ~second.flags) & NopGroup) && (first.write_vector & second.fake_vector));
}

void Rsp::begin_instruction() {
  next_delay_slot_ = false;
  pipeline_pc_ = next_pc_;
  next_pc_ = (pipeline_pc_ + 4) & 0xfff;
}

void Rsp::end_instruction() {
  state_.gpr[0] = 0;
  if (delay_slot_) {
    pipeline_.stall();
    if (pipeline_pc_ & 4)
      pipeline_.single_issue = true;
  }
  delay_slot_ = next_delay_slot_;
  pc_ = pipeline_pc_;
}

std::uint32_t Rsp::execute(std::uint32_t instruction) {
  pipeline_.clocks = 0;
  begin_instruction();
  pipeline_.issue(decode_info(instruction));
  decode(instruction);
  pipeline_.end();
  end_instruction();
  return pipeline_.clocks;
}

std::uint32_t Rsp::step() {
  pipeline_.clocks = 0;
  auto instruction = static_cast<std::uint32_t>(read_local(0x1000 | pc_, 4));
  begin_instruction();
  const auto first = decode_info(instruction);
  pipeline_.issue(first);
  decode(instruction);
  if (!pipeline_.single_issue && !(first.flags & Branch)) {
    instruction = static_cast<std::uint32_t>(read_local(0x1000 | ((pc_ + 4) & 0xfff), 4));
    const auto second = decode_info(instruction);
    if (dual_issue(first, second)) {
      end_instruction();
      begin_instruction();
      pipeline_.issue(second);
      decode(instruction);
    }
  }
  pipeline_.end();
  end_instruction();
  return pipeline_.clocks;
}

void Rsp::advance(std::uint32_t clocks) {
  elapse(clocks);
  run();
}

std::uint32_t Rsp::idle_clocks() const {
  if (dma_busy())
    return 128;
  // Preserve the halted clock phase; pending DMA still advances one quantum at a time.
  const auto quanta = std::uint64_t(-(clock_ + 1)) / 128 + 1;
  return static_cast<std::uint32_t>(std::min(quanta, std::uint64_t(0xffffffffu / 128)) * 128);
}

void Rsp::run() {
  while (clock_ < 0) {
    if (!status_.halted && compiler_->run())
      continue;
    const auto elapsed = status_.halted ? idle_clocks() : step();
    clock_ += elapsed;
    advance_dma(elapsed);
  }
}

void Rsp::run_interpreted() {
  while (clock_ < 0) {
    const auto elapsed = status_.halted ? idle_clocks() : step();
    clock_ += elapsed;
    advance_dma(elapsed);
  }
}

} // namespace cupid::n64
