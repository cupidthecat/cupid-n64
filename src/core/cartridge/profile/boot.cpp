#include "core/cartridge/profile/boot.hpp"
#include <bit>

namespace cupid::n64::cartridge {
namespace {
std::uint32_t fold_product(std::uint32_t value, std::uint32_t multiplier, std::uint32_t fallback) {
  const auto product = std::uint64_t(value) * (multiplier ? multiplier : fallback);
  const auto difference = std::uint32_t(product >> 32) - std::uint32_t(product);
  return difference ? difference : value;
}

struct Signature {
  unsigned seed;
  std::uint64_t checksum;
  CicModel model;
};
constexpr Signature signatures[] = {
    {0x3f, 0x45cc73ee317a, CicModel::N6101}, {0x3f, 0x44160ec5d9af, CicModel::N7102},
    {0x3f, 0xa536c0f1d859, CicModel::N6102}, {0x78, 0x586fd4709867, CicModel::N6103},
    {0x91, 0x8618a45bc2d3, CicModel::N6105}, {0x85, 0x2bbad4e6eb74, CicModel::N6106},
    {0xac, 0x93e983a8f152, CicModel::N5101}, {0xdd, 0x32b294e2ab90, CicModel::N8303},
    {0xdd, 0x6ee8d9e84970, CicModel::N8401}, {0xdd, 0x083c6c77e0b1, CicModel::N5167},
    {0xdd, 0x05ba2ef0a5f1, CicModel::Ddus},
};
} // namespace

std::uint64_t boot_checksum(std::span<const std::uint8_t> boot, unsigned seed, unsigned byte_swap) {
  if (boot.size() < 0xfc0 || (byte_swap != 0 && byte_swap != 1 && byte_swap != 3))
    return 0;
  const auto word = [&](unsigned index) {
    const auto offset = index * 4;
    return (std::uint32_t(boot[offset ^ byte_swap]) << 24) |
           (std::uint32_t(boot[(offset + 1) ^ byte_swap]) << 16) |
           (std::uint32_t(boot[(offset + 2) ^ byte_swap]) << 8) | boot[(offset + 3) ^ byte_swap];
  };
  std::array<std::uint32_t, 16> state;
  state.fill((0x6c078965u * (seed & 255) + 1) ^ word(0));
  auto previous = word(0);
  for (unsigned index = 0; index < 1008; ++index) {
    const auto current = word(index);
    const auto iteration = index + 1;
    const auto rotated_right = std::rotr(current, previous & 31);
    const auto rotated_left = std::rotl(current, previous >> 27);
    state[0] += fold_product(1006u - index, current, iteration);
    state[1] = fold_product(state[1], current, iteration);
    state[2] ^= current;
    state[3] += fold_product(current + 5, 0x6c078965, iteration);
    state[4] += rotated_right;
    state[5] += rotated_left;
    state[6] = current < state[6] ? (state[3] + state[6]) ^ (current + iteration)
                                  : (state[4] + current) ^ state[6];
    state[7] = fold_product(state[7], std::rotl(current, previous & 31), iteration);
    state[8] = fold_product(state[8], std::rotr(current, previous >> 27), iteration);
    state[9] = previous < current ? fold_product(state[9], current, iteration) : state[9] + current;
    if (index != 1007) {
      const auto next = word(index + 1);
      state[10] = fold_product(state[10] + current, next, iteration);
      state[11] = fold_product(state[11] ^ current, next, iteration);
      state[12] += state[8] ^ current;
      state[13] += std::rotr(current, current & 31) + std::rotr(next, next & 31);
      state[14] = fold_product(fold_product(state[14], rotated_right, iteration),
                               std::rotr(next, current & 31), iteration);
      state[15] = fold_product(fold_product(state[15], rotated_left, iteration),
                               std::rotl(next, current >> 27), iteration);
    }
    previous = current;
  }
  std::array<std::uint32_t, 4> result;
  result.fill(state[0]);
  for (unsigned index = 0; index < state.size(); ++index) {
    const auto value = state[index];
    result[0] += std::rotr(value, value & 31);
    result[1] = value < result[0] ? result[1] + value : fold_product(result[1], value, index);
    result[2] = ((value >> 1) & 1) == (value & 1) ? result[2] + value
                                                  : fold_product(result[2], value, index);
    result[3] = value & 1 ? result[3] ^ value : fold_product(result[3], value, index);
  }
  return ((std::uint64_t(fold_product(result[0], result[1], 16)) << 32) | (result[2] ^ result[3])) &
         0xffffffffffff;
}

std::optional<BootChip> identify_boot(std::span<const std::uint8_t> boot) {
  if (boot.size() < 0xfc0)
    return {};
  for (unsigned swap : {0u, 1u, 3u}) {
    unsigned seed = 0;
    std::uint64_t checksum = 0;
    for (const auto &signature : signatures) {
      if (signature.seed != seed) {
        seed = signature.seed;
        checksum = boot_checksum(boot, seed, swap);
      }
      if (checksum == signature.checksum)
        return BootChip{signature.model, swap};
    }
  }
  return {};
}

} // namespace cupid::n64::cartridge
