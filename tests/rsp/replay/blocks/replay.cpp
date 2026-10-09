#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
namespace {
struct Replay : rsp_replay::Probe {
  Replay() : Probe(rsp_replay::blocks::expected, "RSP blocks") {}
  void observe() {
    registers();
    status();
  }
};
} // namespace
void rsp_replay_blocks_tests() {
  Replay p;
  rsp_replay::blocks::scenarios(p);
  p.complete(rsp_replay::blocks::cases, rsp_replay::blocks::observations);
}
} // namespace test
