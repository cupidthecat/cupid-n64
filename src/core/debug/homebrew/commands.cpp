#include "core/debug/diagnostics.hpp"
#include "core/debug/homebrew/command.hpp"
#include "core/system/console.hpp"
#include <bit>

namespace cupid::n64 {

std::optional<std::uint64_t> Diagnostics::extension(DebugProcessor processor,
                                                    std::uint32_t instruction, std::uint64_t rd,
                                                    std::uint64_t rt) {
  const auto command = HomebrewCommand::decode(instruction);
  switch (command.function) {
  case 0x20:
    return command.code == 1   ? HomebrewCommand::capabilities(processor)
           : command.code == 2 ? 0x0e
                               : 0;
  case 0x23:
    if (!emux_trace_)
      previous_rsp_trace_ = trace_->enabled(TraceKind::RspInstruction);
    trace_->set_enabled(TraceKind::RspInstruction, true);
    emux_trace_ = true;
    trace_countdown_ = command.code;
    break;
  case 0x24:
    trace_->set_enabled(TraceKind::RspInstruction, false);
    emux_trace_ = false;
    trace_countdown_ = 0;
    break;
  case 0x25:
    memory_log(processor, rd, rt, command.code);
    break;
  case 0x26:
    register_log(static_cast<std::uint32_t>(rd), command.code);
    break;
  case 0x27:
    memory_dump(processor, rd, rt ? rt : processor == DebugProcessor::Cpu ? 256 : 0);
    break;
  case 0x28:
    profile_command(rd, command.code);
    break;
  case 0x29:
    if (processor == DebugProcessor::Rsp)
      rd = static_cast<std::uint64_t>(
          std::int64_t(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(rd))));
    return rt <= ~0u ? profile_read(rd, static_cast<unsigned>(rt)) : 0;
  case 0x2a:
    exception_mask_ = rt;
    break;
  case 0x2c:
    request_control(command.code);
    break;
  }
  return {};
}

bool Diagnostics::cpu_extension(std::uint32_t instruction) {
  const auto command = HomebrewCommand::decode(instruction);
  if (!homebrew_ || !cpu_ || !console_ || (instruction & 0xfe000000) != 0x42000000 ||
      command.name(DebugProcessor::Cpu).empty())
    return false;
  try {
    if (const auto value = extension(DebugProcessor::Cpu, instruction, cpu_->state_.gpr[command.rd],
                                     cpu_->state_.gpr[command.rt]))
      cpu_->state_.gpr[command.function == 0x20 ? command.rd : command.rt] = *value;
  } catch (...) {
    ++dropped_;
  }
  return true;
}

bool Diagnostics::rsp_extension(std::uint32_t instruction) {
  const auto command = HomebrewCommand::decode(instruction);
  if (!homebrew_ || !console_ || (instruction & 0xfe000000) != 0x42000000 ||
      command.name(DebugProcessor::Rsp).empty())
    return false;
  auto &rsp = console_->signal();
  try {
    if (const auto value = extension(DebugProcessor::Rsp, instruction, rsp.state_.gpr[command.rd],
                                     rsp.state_.gpr[command.rt])) {
      const auto dest = command.function == 0x20 ? command.rd : command.rt;
      if (dest)
        rsp.state_.gpr[dest] = static_cast<std::uint32_t>(*value);
    }
  } catch (...) {
    ++dropped_;
  }
  return true;
}

void Diagnostics::rsp_instruction() noexcept {
  if (homebrew_ && trace_countdown_ && --trace_countdown_ == 0) {
    trace_->set_enabled(TraceKind::RspInstruction, false);
    emux_trace_ = false;
  }
}

void Diagnostics::request_control(unsigned code) {
  if (code < 1 || code > 3)
    return;
  control_ = code == 1   ? HostControl::Exit
             : code == 2 ? HostControl::FastForward
                         : HostControl::NormalSpeed;
  record({.kind = DiagnosticKind::HostControl, .origin = origin(), .code = code});
  if (origin().processor == DebugProcessor::Cpu)
    cpu_->block_exit_ = cpu_->next_block_exit_ = true;
  if (cpu_->synchronize_)
    cpu_->synchronize_();
}

} // namespace cupid::n64
