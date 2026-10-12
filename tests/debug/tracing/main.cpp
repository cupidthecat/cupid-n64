#include "fixture.hpp"
#include <stdexcept>

namespace tracing_test {

void log_tests() {
  TraceLog log;
  log.record({.pc = 10});
  equal(log.records().empty(), true);
  TraceConfig config;
  config.mask = all_traces;
  config.capacity = 3;
  config.cpu = {10, 20};
  config.rsp = {0x100, 0x104};
  log.configure(config);
  for (unsigned n = 0; n < 1000; ++n) {
    log.record({.pc = 9});
    log.record({.kind = TraceKind::RspInstruction, .pc = 0x108});
    log.record({.pc = 10 + n % 11, .instruction = n});
  }
  auto records = log.records();
  equal(records.size(), 3);
  equal(log.dropped(), 997);
  for (unsigned n = 0; n < records.size(); ++n) {
    equal(records[n].sequence, 998 + n);
    equal(records[n].instruction, 997 + n);
  }
  log.stop();
  log.record({.pc = 10});
  equal(log.records() == records, true);
  auto invalid = config;
  invalid.capacity = 0;
  for (unsigned scenario = 0; scenario < 5; ++scenario) {
    invalid = config;
    if (scenario == 0)
      invalid.capacity = 0;
    if (scenario == 1)
      invalid.capacity = TraceLog::maximum_records + 1;
    if (scenario == 2)
      invalid.cpu = {20, 10};
    if (scenario == 3)
      invalid.rsp.last = 4096;
    if (scenario == 4)
      invalid.mask = 0x80000000;
    bool rejected = false;
    try {
      log.configure(invalid);
    } catch (const std::exception &) {
      rejected = true;
    }
    equal(rejected, true);
    equal(log.records() == records, true);
  }
  log.configure(config);
  log.record({.kind = TraceKind::RspInstruction, .pc = 0x104});
  equal(log.records().size(), 1);
  log.clear();
  equal(log.records().empty(), true);
  equal(log.dropped(), 0);
  log.configure({.mask = trace_bit(TraceKind::CpuInstruction), .capacity = 512});
  for (unsigned n = 0; n < 512; ++n)
    log.record({.instruction = 0x1000ffff});
  const auto text = cupid::desktop::trace_dump(log.records(), 512);
  equal(std::count(text.begin(), text.end(), '\n'), 256);
  equal(text.find("257  CPU clock") == 0, true);
  equal(text.size() < 65536, true);
}

} // namespace tracing_test

int main() {
  tracing_test::log_tests();
  tracing_test::instruction_tests();
  tracing_test::execution_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
