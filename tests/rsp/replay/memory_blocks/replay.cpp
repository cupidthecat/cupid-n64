#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
namespace {
struct Replay : rsp_replay::Probe {
  Replay() : Probe(rsp_replay::memory_blocks::expected, "RSP memory_blocks") {}
  void observe() {
    registers();
    status();
    memory();
  }
};
} // namespace
void rsp_replay_memory_blocks_tests() {
  Replay p;
  rsp_replay::memory_blocks::scenarios(p);
  p.complete(rsp_replay::memory_blocks::cases, rsp_replay::memory_blocks::observations);
}
} // namespace test
