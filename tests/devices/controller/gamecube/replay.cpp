#include "../../fixture.hpp"
#include "core/controller/gamecube/gamecube.hpp"
#include "expected.hpp"
#include "scenarios.hpp"

namespace test {
namespace {

struct Probe {
  cupid::n64::GameCubePad pad;
  unsigned block = 0;
  std::uint64_t hash = 0xcbf29ce484222325ull, observations = 0;

  void reset() {
    pad.reset();
  }
  void input(gamecube::Input input) {
    pad.input_host({input.buttons, input.x, input.y, input.cx, input.cy, input.l, input.r});
  }
  void emit(std::uint64_t value) {
    for (unsigned byte = 0; byte < 8; ++byte) {
      hash ^= (value >> (byte * 8)) & 255;
      hash *= 0x100000001b3ull;
    }
    ++observations;
  }
  void command(const std::array<std::uint8_t, 6> &request, unsigned send, unsigned receive) {
    std::array<std::uint8_t, 16> output;
    output.fill(0xa5);
    const auto status =
        pad.communicate(std::span(request).first(send), std::span(output).first(receive));
    emit(status.valid);
    emit(status.overflow);
    emit(pad.rumbling());
    for (auto byte : output)
      emit(byte);
  }
  void finish() {
    equal(block < gamecube::expected.size(), true);
    if (block < gamecube::expected.size())
      equal(hash, gamecube::expected[block]);
    ++block;
    hash = 0xcbf29ce484222325ull;
  }
};

} // namespace

void gamecube_replay_tests() {
  Probe p;
  gamecube::scenarios(p);
  equal(p.block, gamecube::expected.size());
  equal(p.observations, gamecube::observations);
}

} // namespace test
