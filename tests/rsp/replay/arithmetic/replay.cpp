#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
namespace {
struct Replay : rsp_replay::Probe {
  Replay() : Probe(rsp_replay::arithmetic::expected, "RSP arithmetic") {}
  void observe() {
    registers();
  }
};
} // namespace
void rsp_replay_arithmetic_tests() {
  Replay p;
  rsp_replay::arithmetic::scenarios(p);
  p.complete(rsp_replay::arithmetic::cases, rsp_replay::arithmetic::observations);
}
} // namespace test
