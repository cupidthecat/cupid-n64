#pragma once
#include "../decode/scenarios.hpp"
namespace test::cpu_replay::priority {
constexpr std::array modes{0x10000000u, 0x10000001u, 0x10000003u, 0x10000005u, 0x10000007u,
                           0x10000011u, 0x10000031u, 0x10000009u, 0x10000049u, 0x10300001u,
                           0x10400001u, 0x10400003u, 0x10400005u, 0x10000081u};
constexpr std::array masks{0u, 255u, 1u, 4u, 128u, 85u, 170u};
constexpr std::array operations{0x0000000cu, 0x0000000du, 0x8c220001u,
                                0x24020001u, 0x40026000u, 0x4a000345u};
template <class P>
void run(P &p, bool native, unsigned mode, unsigned mask, unsigned pending, bool nmi, bool delay,
         unsigned opcode) {
  const auto status = mode | mask << 8;
  decode::prepare(p, status, (pending ^ mask) & 15, opcode);
  p.mapping(status);
  const std::uint64_t start = mode & 0x18 ? 0x40000ull : 0xffffffff80001000ull;
  p.control(14, start);
  p.control(30, start);
  p.gpr(1, 0x50000);
  p.gpr(26, start + 0x200);
  for (unsigned word = 0; word < 32; ++word)
    p.code(word * 4, 0);
  p.code(0, delay ? 0x10000002u : opcode);
  p.code(4, delay ? opcode : 26u << 21 | 8u);
  p.code(12, 26u << 21 | 8u);
  p.code(64, 0x42000018u);
  p.pc(start);
  if (delay)
    p.execute(0x10000002u);
  if (nmi)
    p.nmi();
  for (unsigned bit = 0; bit < 8; ++bit)
    p.interrupt(bit, pending >> bit & 1);
  for (auto value :
       {std::uint64_t(native), std::uint64_t(mode), std::uint64_t(mask), std::uint64_t(pending),
        std::uint64_t(nmi), std::uint64_t(delay), std::uint64_t(opcode), start})
    p.emit(value);
  for (unsigned offset = 0; offset < 128; offset += 4)
    p.emit(p.word(offset));
  p.observe();
  p.single(native);
  p.observe();
  p.pc(start + 64);
  p.single(native);
  p.observe();
  p.single(native);
  p.observe();
  p.finish();
}
template <class P> void scenarios(P &p) {
  for (bool native : {false, true})
    for (auto mode : modes)
      for (auto mask : masks)
        for (unsigned pending = 0; pending < 256; ++pending)
          for (bool nmi : {false, true})
            for (bool delay : {false, true})
              run(p, native, mode, mask, pending, nmi, delay, 0x48000000u);
  for (bool native : {false, true})
    for (auto mode : modes)
      for (auto mask : masks)
        for (unsigned pending : {0u, 1u, 4u, 128u, 255u})
          for (bool nmi : {false, true})
            for (bool delay : {false, true})
              for (auto opcode : operations)
                run(p, native, mode, mask, pending, nmi, delay, opcode);
}
} // namespace test::cpu_replay::priority
