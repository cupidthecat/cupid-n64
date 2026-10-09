#pragma once
#include "../memory/scenarios.hpp"
#include <array>
namespace test::rsp_replay::memory_blocks {
template <class P> void scenarios(P &p) {
  for (bool native : {false, true})
    for (bool store : {false, true})
      for (unsigned operation = 0; operation < 12; ++operation)
        for (unsigned element = 0; element < 16; ++element)
          for (unsigned offset = 0; offset < 16; ++offset) {
            p.begin(native);
            const auto address = 0xfffffff0u + offset;
            const auto immediate = offset < 4 ? 0u : offset < 8 ? 1u : offset < 12 ? 127u : 64u;
            memory::prepare(p, operation, element, offset, address);
            const auto target = (element * 3 + offset * 7) & 31;
            const auto instruction = (store ? 58u : 50u) << 26 | 13u << 21 | target << 16 |
                                     operation << 11 | element << 7 | immediate;
            const auto start = std::array{0u, 4u, 0xfd8u, 0xffcu}[offset & 3];
            const std::array instructions{instruction,
                                          0x24630001u,
                                          0x48070000u | target << 11 | element << 7,
                                          0x4a000000u | 0x2cu | ((target + 1) & 31) << 6 |
                                              target << 11 | ((target + 2) & 31) << 16,
                                          0x48880000u | ((target + 3) & 31) << 11 | element << 7,
                                          0x4a000037u,
                                          58u << 26 | 13u << 21 | target << 16 | 3u << 11 |
                                              element << 7 | 2u,
                                          0x10000002u,
                                          0x24a50001u,
                                          0x24091234u,
                                          13u,
                                          0u};
            for (unsigned index = 0; index < instructions.size(); ++index)
              p.code(start + index * 4, instructions[index]);
            p.set_pc(start);
            p.io_write(16, 1);
            p.run(1);
            p.observe();
            p.run(4096);
            p.observe();
            p.finish();
          }
}
} // namespace test::rsp_replay::memory_blocks
