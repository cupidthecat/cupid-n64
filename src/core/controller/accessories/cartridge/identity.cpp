#include "identity.hpp"
#include <bit>

namespace cupid::n64::cartridge_identity {

std::array<std::uint32_t, 8> sha256(std::span<const std::uint8_t> data) {
  constexpr std::array<std::uint32_t, 64> constants{
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
      0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
      0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
      0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
      0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
      0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
      0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
      0xc67178f2};
  std::array<std::uint32_t, 8> digest{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const auto padded = ((data.size() + 72) / 64) * 64;
  const auto bits = std::uint64_t(data.size()) * 8;
  for (std::size_t offset = 0; offset < padded; offset += 64) {
    std::array<std::uint32_t, 64> words{};
    for (unsigned byte = 0; byte < 64; ++byte) {
      const auto position = offset + byte;
      std::uint8_t value = 0;
      if (position < data.size())
        value = data[position];
      else if (position == data.size())
        value = 0x80;
      else if (position >= padded - 8)
        value = static_cast<std::uint8_t>(bits >> ((padded - position - 1) * 8));
      words[byte / 4] |= std::uint32_t(value) << ((3 - byte % 4) * 8);
    }
    for (unsigned word = 16; word < 64; ++word) {
      const auto x = words[word - 15], y = words[word - 2];
      const auto low = std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
      const auto high = std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10);
      words[word] = words[word - 16] + low + words[word - 7] + high;
    }
    auto state = digest;
    for (unsigned round = 0; round < 64; ++round) {
      const auto first =
          state[7] + (std::rotr(state[4], 6) ^ std::rotr(state[4], 11) ^ std::rotr(state[4], 25)) +
          ((state[4] & state[5]) ^ (~state[4] & state[6])) + constants[round] + words[round];
      const auto second =
          (std::rotr(state[0], 2) ^ std::rotr(state[0], 13) ^ std::rotr(state[0], 22)) +
          ((state[0] & state[1]) ^ (state[0] & state[2]) ^ (state[1] & state[2]));
      for (unsigned reg = 7; reg; --reg)
        state[reg] = state[reg - 1];
      state[4] += first;
      state[0] = first + second;
    }
    for (unsigned word = 0; word < 8; ++word)
      digest[word] += state[word];
  }
  return digest;
}

bool multicart(std::span<const std::uint8_t> rom) {
  constexpr std::array<std::array<std::uint32_t, 8>, 5> identities{{
      {0x85f6b7b2, 0xeb182f53, 0xb00ac3e9, 0xe4a86ef8, 0xf7496550, 0xa0b2f645, 0x73ab61f6,
       0x70e30245},
      {0x40c3b05b, 0x76355d9a, 0x4e244cb2, 0x2c074648, 0xf0393357, 0x1b023f77, 0xf667e8b2,
       0xe4b48513},
      {0xdb3a4536, 0x3490b8cb, 0x56274f26, 0x0358c7b8, 0xdc0ffc0a, 0x41517123, 0xd6c5d78c,
       0x2c54dc38},
      {0xc6c43dce, 0x0514016a, 0x4a635db3, 0xec250bfa, 0x57904220, 0x42599457, 0xcd30841e,
       0xc92f5979},
      {0xcacace09, 0x74a588c6, 0x8766bbf2, 0x1a631be9, 0xfa234335, 0xe0607ebf, 0xa9de77b3,
       0xdd0cbf18},
  }};
  const auto identity = sha256(rom);
  for (const auto &known : identities)
    if (identity == known)
      return true;
  return false;
}

} // namespace cupid::n64::cartridge_identity
