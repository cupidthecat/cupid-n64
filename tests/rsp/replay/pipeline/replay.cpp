#include "../fixture.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
namespace {
struct Replay : rsp_replay::Probe {
  Replay() : Probe(rsp_replay::pipeline::expected, "RSP pipeline") {}
  void observe() {
    registers();
    status();
    emit(f.mi.read_word(8));
    memory();
  }
};
} // namespace
void rsp_replay_pipeline_tests() {
  Replay p;
  rsp_replay::pipeline::scenarios(p);
  p.complete(rsp_replay::pipeline::cases, rsp_replay::pipeline::observations);
}
} // namespace test
