#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
namespace {
struct Replay : rsp_replay::Probe {
  Replay() : Probe(rsp_replay::memory::expected, "RSP memory") {}
  void observe() {
    registers();
    memory();
  }
};
} // namespace
void rsp_replay_memory_tests() {
  Replay p;
  rsp_replay::memory::scenarios(p);
  p.complete(rsp_replay::memory::cases, rsp_replay::memory::observations);
}
} // namespace test
