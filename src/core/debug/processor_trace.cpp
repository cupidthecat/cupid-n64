#include "core/cpu/cpu.hpp"
#include "core/rsp/rsp.hpp"

namespace cupid::n64 {

void Cpu::trace_instruction(std::uint64_t pc, std::uint32_t instruction,
                            unsigned fetch_clocks) const {
  if (instruction_tracing())
    trace_->record({.kind = TraceKind::CpuInstruction,
                    .clock = state_.clocks + fetch_clocks,
                    .pc = pc,
                    .instruction = instruction,
                    .delay_slot = delay_slot_});
}

void Cpu::trace_tlb(TlbEvent event, std::uint64_t address, std::uint64_t physical) const {
  if (trace_ && trace_->enabled(TraceKind::Tlb))
    trace_->record({.kind = TraceKind::Tlb,
                    .tlb = event,
                    .clock = state_.clocks,
                    .pc = state_.pc,
                    .address = address,
                    .value = physical,
                    .delay_slot = delay_slot_});
}

void Rsp::trace_instruction(std::uint32_t pc, std::uint32_t instruction,
                            unsigned pending_clocks) const {
  if (instruction_tracing())
    trace_->record({.kind = TraceKind::RspInstruction,
                    .clock = static_cast<std::uint64_t>(clock_) + pending_clocks,
                    .pc = pc,
                    .instruction = instruction,
                    .delay_slot = delay_slot_});
}

} // namespace cupid::n64
