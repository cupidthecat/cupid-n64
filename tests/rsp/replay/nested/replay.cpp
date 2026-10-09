#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
namespace {
struct Replay : rsp_replay::Probe {
  Replay() : Probe(rsp_replay::nested::expected, "RSP nested") {}
  void observe() {
    registers();
    status();
    emit(f.mi.read_word(8));
    memory();
  }
};
} // namespace
void rsp_replay_nested_tests() {
  Replay p;
  rsp_replay::nested::scenarios(p);
  p.complete(rsp_replay::nested::cases, rsp_replay::nested::observations);
}
} // namespace test
