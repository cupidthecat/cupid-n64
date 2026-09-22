#include "cupid/cic.hpp"

#include <algorithm>
#include <bit>

namespace cupid {
namespace {

struct SecurityPart {
    CicModel model;
    u8 seed;
    u64 checksum;
};

constexpr std::array security_parts{
    SecurityPart{CicModel::Nus6101, 0x3f, 0x45cc73ee317aULL},
    SecurityPart{CicModel::Nus6102, 0x3f, 0xa536c0f1d859ULL},
    SecurityPart{CicModel::Nus7102, 0x3f, 0x44160ec5d9afULL},
    SecurityPart{CicModel::Nus6103, 0x78, 0x586fd4709867ULL},
    SecurityPart{CicModel::Nus6105, 0x91, 0x8618a45bc2d3ULL},
    SecurityPart{CicModel::Nus6106, 0x85, 0x2bbad4e6eb74ULL},
    SecurityPart{CicModel::Nus5101, 0xac, 0x93e983a8f152ULL},
    SecurityPart{CicModel::Nus5167, 0xdd, 0x083c6c77e0b1ULL},
    SecurityPart{CicModel::Nus8303, 0xdd, 0x32b294e2ab90ULL},
    SecurityPart{CicModel::Nus8401, 0xdd, 0x6ee8d9e84970ULL},
    SecurityPart{CicModel::NusDDUS, 0xde, 0x05ba2ef0a5f1ULL},
};

u32 fold_product(u32 multiplicand, u32 multiplier, u32 fallback) {
    const u64 product = static_cast<u64>(multiplicand) * (multiplier != 0 ? multiplier : fallback);
    const u32 difference = static_cast<u32>(product >> 32) - static_cast<u32>(product);
    return difference != 0 ? difference : multiplicand;
}

template <std::size_t Size> void scramble(std::array<u8, Size>& nibbles, unsigned rounds) {
    for (unsigned pass = 0; pass < rounds; ++pass) {
        for (std::size_t index = 1; index < Size; ++index) {
            nibbles[index] = static_cast<u8>((nibbles[index] + nibbles[index - 1] + 1) & 15);
        }
    }
}

bool pal_region(u8 code) {
    switch (code) {
    case 'D':
    case 'F':
    case 'H':
    case 'I':
    case 'L':
    case 'P':
    case 'S':
    case 'U':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
        return true;
    default:
        return false;
    }
}

} // namespace

u64 Cic::boot_checksum(std::span<const u8> bootcode, u8 seed) {
    constexpr std::size_t word_count = 1008;
    if (bootcode.size() != word_count * 4)
        return 0;
    std::array<u32, word_count> words{};
    for (std::size_t index = 0; index < words.size(); ++index) {
        words[index] = read_be32(bootcode.data() + index * 4);
    }

    std::array<u32, 16> state{};
    state.fill((0x6c078965U * seed + 1U) ^ words[0]);
    for (u32 index = 0; index < word_count; ++index) {
        const u32 position = index + 1;
        const u32 current = words[index];
        const u32 previous = words[index != 0 ? index - 1 : 0];
        const int low_shift = static_cast<int>(previous & 31U);
        const int high_shift = static_cast<int>(previous >> 27);
        const u32 rotated_low = std::rotl(current, low_shift);
        const u32 rotated_high = std::rotr(current, high_shift);

        state[0] += fold_product(1007U - position, current, position);
        state[1] = fold_product(state[1], current, position);
        state[2] ^= current;
        state[3] += fold_product(current + 5U, 0x6c078965U, position);
        state[4] += std::rotr(current, low_shift);
        state[5] += std::rotl(current, high_shift);
        state[6] = current < state[6] ? (state[3] + state[6]) ^ (current + position)
                                      : (state[4] + current) ^ state[6];
        state[7] = fold_product(state[7], rotated_low, position);
        state[8] = fold_product(state[8], rotated_high, position);
        state[9] = previous < current ? fold_product(state[9], current, position) : state[9] + current;

        if (index + 1 == word_count)
            continue;
        const u32 next = words[index + 1];
        state[10] = fold_product(state[10] + current, next, position);
        state[11] = fold_product(state[11] ^ current, next, position);
        state[12] += state[8] ^ current;
        state[13] += std::rotr(current, static_cast<int>(current & 31U)) +
                     std::rotr(next, static_cast<int>(next & 31U));
        state[14] = fold_product(fold_product(state[14], std::rotr(current, low_shift), position),
                                 std::rotr(next, static_cast<int>(current & 31U)), position);
        state[15] = fold_product(fold_product(state[15], std::rotl(current, high_shift), position),
                                 std::rotl(next, static_cast<int>(current >> 27)), position);
    }

    u32 rotation_sum = state[0];
    u32 ordered_fold = state[0];
    u32 bit_fold = state[0];
    u32 parity_fold = state[0];
    for (u32 index = 0; index < state.size(); ++index) {
        const u32 value = state[index];
        rotation_sum += std::rotr(value, static_cast<int>(value & 31U));
        ordered_fold = value < rotation_sum ? ordered_fold + value : fold_product(ordered_fold, value, index);
        bit_fold =
            ((value >> 1) & 1U) == (value & 1U) ? bit_fold + value : fold_product(bit_fold, value, index);
        parity_fold = (value & 1U) != 0 ? parity_fold ^ value : fold_product(parity_fold, value, index);
    }
    return ((static_cast<u64>(fold_product(rotation_sum, ordered_fold, 16)) << 32) |
            (parity_fold ^ bit_fold)) &
           0xffffffffffffULL;
}

void Cic::configure(CicModel model, bool pal) {
    const auto part = std::find_if(security_parts.begin(), security_parts.end(),
                                   [model](const SecurityPart& entry) { return entry.model == model; });
    if (part == security_parts.end())
        return;
    model_ = model;
    seed_ = part->seed;
    checksum_ = part->checksum;
    pal_ = model == CicModel::Nus7102 || (model != CicModel::Nus6101 && pal);
    recognized_ = true;
}

bool Cic::detect(std::span<const u8> normalized_rom) {
    if (normalized_rom.size() < 0x1000)
        return false;
    const bool region = pal_region(normalized_rom[0x3e]);
    std::array<u64, 256> computed{};
    std::array<bool, 256> tested{};
    for (const auto& part : security_parts) {
        if (!tested[part.seed]) {
            computed[part.seed] = boot_checksum(normalized_rom.subspan(0x40, 0xfc0), part.seed);
            tested[part.seed] = true;
        }
        if (computed[part.seed] == part.checksum) {
            configure(part.model, region);
            return true;
        }
    }
    configure(CicModel::Nus6102, region);
    recognized_ = false;
    return false;
}

void Cic::reset() {
    // Power cycling the console does not change the security part on its cartridge.
}

bool Cic::disk_drive() const {
    return model_ == CicModel::Nus8303 || model_ == CicModel::Nus8401 || model_ == CicModel::NusDDUS;
}

bool Cic::verify_checksum(std::span<const u8, 6> bytes) const {
    u64 supplied = 0;
    for (const u8 byte : bytes)
        supplied = (supplied << 8) | byte;
    return supplied == checksum_;
}

std::array<u8, 6> Cic::seed_packet() const {
    std::array<u8, 6> packet{0xb,
                             5,
                             static_cast<u8>(seed_ >> 4),
                             static_cast<u8>(seed_ & 15U),
                             static_cast<u8>(seed_ >> 4),
                             static_cast<u8>(seed_ & 15U)};
    scramble(packet, 2);
    return packet;
}

std::array<u8, 16> Cic::checksum_packet(u16 entropy) const {
    std::array<u8, 16> packet{};
    for (unsigned nibble = 0; nibble < 4; ++nibble) {
        packet[nibble] = static_cast<u8>((entropy >> ((3U - nibble) * 4)) & 15U);
    }
    for (unsigned nibble = 0; nibble < 12; ++nibble) {
        packet[nibble + 4] = static_cast<u8>((checksum_ >> ((11U - nibble) * 4)) & 15U);
    }
    scramble(packet, 4);
    return packet;
}

void Cic::challenge(std::span<u8, 15> bytes) const {
    if (model_ != CicModel::Nus6105) {
        for (u8& byte : bytes)
            byte = static_cast<u8>(~byte);
        return;
    }

    constexpr std::array<std::array<u8, 16>, 2> transition{{
        {{4, 7, 10, 7, 14, 5, 14, 1, 12, 15, 8, 15, 6, 3, 6, 9}},
        {{4, 1, 10, 7, 14, 5, 14, 1, 12, 9, 8, 5, 6, 3, 12, 9}},
    }};
    u8 key = 11;
    unsigned table = 0;
    for (u8& byte : bytes) {
        const u8 input = byte;
        byte = 0;
        for (unsigned half = 0; half < 2; ++half) {
            const unsigned shift = (1 - half) * 4;
            const unsigned challenge_nibble = (input >> shift) & 15U;
            const u8 response = static_cast<u8>((key + challenge_nibble * 5) & 15U);
            byte = static_cast<u8>(byte | (response << shift));
            key = transition[table][response];
            const unsigned sign = response >> 3;
            const unsigned magnitude = (response & 7U) ^ (sign != 0 ? 7U : 0U);
            unsigned next_table = sign ^ static_cast<unsigned>(magnitude % 3 != 1);
            if (table == 1) {
                if (response == 1 || response == 9)
                    next_table = 1;
                else if (response == 11 || response == 14)
                    next_table = 0;
            }
            table = next_table;
        }
    }
}

} // namespace cupid
