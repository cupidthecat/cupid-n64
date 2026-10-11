#include "check.hpp"
#include <bit>

namespace test::machine_replay {

void input_tests() {
  Input input;
  input.expansion = false;
  const std::uint32_t words[] = {i(9, 0, 4, 42), r(8, 31, 0, 0), 0};
  code(input, words);
  input.initial.push_back({ActionKind::CpuRegister, 7, 19});
  input.initial.push_back({ActionKind::WriteMemory, 0x2800, 0xabcdef01});
  input.actions = {{ActionKind::Controller, 0, 0x12ab8030},
                   {ActionKind::CpuBlock, 0, 1},
                   {ActionKind::CpuRegister, 9, 23}};
  const auto bytes = encode(input);
  equal(encode(decode(bytes)) == bytes, true);
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    bool rejected = false;
    try {
      decode(std::span(bytes).first(length));
    } catch (const cupid::n64::state::InvalidState &) {
      rejected = true;
    }
    equal(rejected, true);
  }
  for (const auto action :
       {Action{static_cast<ActionKind>(255)}, Action{ActionKind::CpuBlock, 0, 1'000'001},
        Action{ActionKind::CpuRegister, 32, 1}, Action{ActionKind::RspLocal, 0x1fff, 1, 8}}) {
    auto invalid = input;
    invalid.actions.push_back(action);
    bool rejected = false;
    try {
      encode(invalid);
    } catch (const cupid::n64::state::InvalidState &) {
      rejected = true;
    }
    equal(rejected, true);
  }
  const Observer wrong_result = [](cupid::n64::Console &console, Mode mode, std::size_t) {
    if (mode == Mode::Native && console.cpu().state().gpr[4] == 42)
      console.cpu().state().gpr[4] ^= 1;
  };
  const auto mismatch = compare(input, wrong_result);
  equal(mismatch.has_value(), true);
  if (!mismatch)
    return;
  equal(mismatch->boundary, 2);
  equal(mismatch->state.field == "cpu.gpr", true);
  equal(mismatch->state.index, 4);
  equal(mismatch->state.expected, 42);
  equal(mismatch->state.actual, 43);
  const auto fails = [&](const Input &candidate) {
    const auto difference = compare(candidate, wrong_result);
    return difference && difference->state.field == "cpu.gpr" && difference->state.index == 4;
  };
  const auto reduced = reduce(input, fails);
  equal(reduced.initial.size() < input.initial.size(), true);
  equal(reduced.actions.size(), 1);
  const auto small = encode(reduced);
  equal(small.size() < 128, true);
  equal(fails(decode(small)), true);
  equal(compare(reduced).has_value(), false);
  auto absent_input = reduced;
  absent_input.contracts = {{0, {"cpu.gpr", 4, 0, 1, 8}}};
  const auto absent = compare(absent_input);
  equal(absent.has_value(), true);
  if (absent) {
    equal(absent->boundary, 0);
    equal(absent->state.field == "cpu.gpr.contract", true);
    equal(absent->state.index, 4);
  }
  for (bool initial : {false, true}) {
    const auto count = initial ? reduced.initial.size() : reduced.actions.size();
    for (std::size_t n = 0; n < count; ++n) {
      auto candidate = reduced;
      auto &actions = initial ? candidate.initial : candidate.actions;
      actions.erase(actions.begin() + n);
      equal(fails(candidate), false);
    }
  }
  const auto bad_clock = compare(input, [](cupid::n64::Console &console, Mode mode, std::size_t) {
    if (mode == Mode::Native)
      console.cpu().advance_clocks(1);
  });
  equal(bad_clock.has_value(), true);
  if (bad_clock) {
    equal(bad_clock->boundary, 1);
    equal(bad_clock->state.field == "cpu.clocks", true);
    equal(bad_clock->state.expected, 0);
    equal(bad_clock->state.actual, 1);
  }
  {
    Input contracted;
    contracted.expansion = false;
    contracted.initial = {{ActionKind::CpuRegister, 17, 5},
                          {ActionKind::WriteMemory, 0x2800, 0xabcdef01}};
    contracted.actions = {{ActionKind::Controller, 0, 0x12348000},
                          {ActionKind::CpuRegister, 9, 23},
                          {ActionKind::CpuRegister, 11, 19}};
    for (unsigned boundary = 1; boundary <= 3; ++boundary)
      contracted.contracts.push_back({boundary, {"cpu.gpr", 5, 0, 1, 8}});
    const Observer fault = [](cupid::n64::Console &console, Mode mode, std::size_t) {
      if (mode == Mode::Native) {
        console.cpu().state().gpr[5] = 1;
        if (console.cpu().state().gpr[9] == 23)
          console.cpu().state().gpr[7] = 1;
      }
    };
    const auto fails_contract = [&](const Input &candidate) {
      const auto difference = compare(candidate, fault);
      return difference && difference->state.field == "cpu.gpr" && difference->state.index == 7;
    };
    equal(fails_contract(contracted), true);
    const auto reduced_contract = reduce(contracted, fails_contract);
    equal(reduced_contract.initial.size(), 0);
    equal(reduced_contract.actions.size(), 1);
    equal(reduced_contract.contracts.size(), 1);
    if (!reduced_contract.contracts.empty())
      equal(reduced_contract.contracts.front().boundary, 1);
    const auto encoded = encode(reduced_contract);
    equal(encoded.size() < 128, true);
    equal(encode(decode(encoded)) == encoded, true);
    equal(fails_contract(decode(encoded)), true);
    equal(compare(decode(encoded),
                  [](cupid::n64::Console &console, Mode mode, std::size_t) {
                    if (mode == Mode::Native)
                      console.cpu().state().gpr[5] = 1;
                  })
              .has_value(),
          false);
    for (unsigned group : {0u, 1u}) {
      auto candidate = reduced_contract;
      if (group == 0) {
        candidate.actions.clear();
        candidate.contracts.clear();
      } else
        candidate.contracts.clear();
      equal(fails_contract(candidate), false);
    }
    for (const auto contract :
         {Difference{2, {"cpu.gpr", 5, 0, 1, 8}}, Difference{1, {"", 5, 0, 1, 8}},
          Difference{1, {"cpu.gpr", 5, 0, 1, 9}}, Difference{1, {"cpu.gpr", 5, 0, 0, 8}}}) {
      auto invalid = reduced_contract;
      invalid.contracts = {contract};
      bool rejected = false;
      try {
        encode(invalid);
      } catch (const cupid::n64::state::InvalidState &) {
        rejected = true;
      }
      equal(rejected, true);
    }
  }
}

} // namespace test::machine_replay
