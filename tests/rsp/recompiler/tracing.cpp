#include "../fixture.hpp"
#include "tracing/cases.hpp"
#include <memory>

namespace test {
using namespace cupid::n64;

void rsp_trace_context_tests() {
  for (bool regenerate : {false, true})
    for (const auto &[modes, pc, ticks, counts] : tracing_context::cases) {
      auto fixture = std::make_unique<RspFixture>();
      auto &rsp = fixture->rsp;
      TraceLog trace;
      rsp.connect_trace(&trace);
      const std::array<std::uint32_t, 3> code{0x243f0001, 0x08000000 | (pc >> 2), 0x8c010000};
      rsp.write_local(0, 4, 41);
      for (unsigned index = 0; index < code.size(); ++index)
        rsp.write_word(0x1000 | ((pc + index * 4) & 0xfff), code[index]);
      rsp.write_status(0, pc);
      rsp.write_io(16, 1);
      for (unsigned stage = 0; stage < ticks.size(); ++stage) {
        if (regenerate && stage == 2) {
          rsp.set_vector_execution(Rsp::VectorExecution::Scalar);
          rsp.set_vector_execution(Rsp::VectorExecution::Simd);
        }
        trace.set_enabled(TraceKind::RspInstruction, (modes >> (stage & 1)) & 1);
        rsp.advance(static_cast<unsigned>(rsp.clocks() + 1));
        equal(rsp.clocks() + 1, ticks[stage]);
        equal(rsp.pc(), pc);
        equal(rsp.state().gpr[1], 41);
        equal(rsp.state().gpr[31], stage ? 42 : 1);
        equal(rsp.status().halted, false);
        equal(rsp.status().broken, false);
        const auto records = trace.records();
        equal(records.size(), counts[stage]);
        for (unsigned index = 0; index < records.size(); ++index) {
          equal(records[index].pc, (pc + index % 3 * 4) & 0xfff);
          equal(records[index].instruction, code[index % 3]);
        }
      }
    }
}

} // namespace test
