#pragma once

#include "../devices/fixture.hpp"
#include "core/rsp/rsp.hpp"

namespace test {

struct RspFixture : MemoryFixture {
  cupid::n64::Rsp rsp{ram, mi, random};
};

constexpr std::uint32_t vector(unsigned operation, unsigned dest, unsigned source, unsigned target,
                               unsigned element = 0) {
  return (18u << 26) | ((16 | element) << 21) | (target << 16) | (source << 11) | (dest << 6) |
         operation;
}

constexpr std::uint32_t vector_memory(bool store, unsigned operation, unsigned target,
                                      unsigned source, unsigned element, std::uint8_t immediate) {
  return ((store ? 58u : 50u) << 26) | (source << 21) | (target << 16) | (operation << 11) |
         (element << 7) | (immediate & 127);
}

void rsp_scalar_tests();
void rsp_vector_tests();
void rsp_simd_tests();
void rsp_native_tests();
void rsp_native_memory_tests();
void rsp_native_vector_tests();
void rsp_block_tests();
void rsp_memory_tests();
void rsp_pipeline_tests();

} // namespace test
