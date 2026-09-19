#include "cupid/cic.hpp"
#include "test.hpp"

#include <array>
#include <vector>

namespace {

using namespace cupid;

template <std::size_t Size> void descramble(std::array<u8, Size>& packet, unsigned rounds) {
    for (unsigned pass = 0; pass < rounds; ++pass) {
        for (std::size_t index = Size - 1; index != 0; --index) {
            packet[index] = static_cast<u8>((packet[index] - packet[index - 1] - 1) & 15);
        }
    }
}

} // namespace

TEST(cic_seed_packets_preserve_cartridge_seed_after_power_cycle) {
    Cic cic;
    cic.configure(CicModel::Nus6105);
    cic.reset();
    CHECK_EQ(cic.seed(), 0x91U);
    auto packet = cic.seed_packet();
    descramble(packet, 2);
    const std::array<u8, 6> expected{0xb, 5, 9, 1, 9, 1};
    CHECK_EQ(packet, expected);
}

TEST(cic_checksum_packet_encodes_all_48_bits) {
    Cic cic;
    cic.configure(CicModel::Nus6103, true);
    auto packet = cic.checksum_packet(0xa123);
    descramble(packet, 4);
    CHECK_EQ(packet[0], 0xaU);
    CHECK_EQ(packet[1], 1U);
    CHECK_EQ(packet[2], 2U);
    CHECK_EQ(packet[3], 3U);
    u64 checksum = 0;
    for (std::size_t index = 4; index < packet.size(); ++index)
        checksum = (checksum << 4) | packet[index];
    CHECK_EQ(checksum, 0x586fd4709867ULL);
    CHECK(cic.pal());
}

TEST(cic_checksum_mismatch_is_rejected) {
    Cic cic;
    std::array<u8, 6> bytes{0xa5, 0x36, 0xc0, 0xf1, 0xd8, 0x59};
    CHECK(cic.verify_checksum(bytes));
    bytes[5] ^= 1;
    CHECK(!cic.verify_checksum(bytes));
    cic.configure(CicModel::Nus6105);
    bytes = {0x86, 0x18, 0xa4, 0x5b, 0xc2, 0xd3};
    CHECK(cic.verify_checksum(bytes));
}

TEST(cic_standard_challenge_inverts_every_bit) {
    Cic cic;
    std::array<u8, 15> data{};
    for (unsigned byte = 0; byte < data.size(); ++byte)
        data[byte] = static_cast<u8>(byte * 17);
    const auto original = data;
    cic.challenge(data);
    for (unsigned byte = 0; byte < data.size(); ++byte)
        CHECK_EQ(data[byte], static_cast<u8>(original[byte] ^ 255U));
    cic.challenge(data);
    CHECK_EQ(data, original);
}

TEST(cic_6105_challenge_carries_state_between_nibbles) {
    Cic cic;
    cic.configure(CicModel::Nus6105);
    std::array<u8, 15> data{};
    cic.challenge(data);
    const std::array<u8, 15> expected{
        0xbf, 0x9f, 0x9f, 0x9f, 0x9f, 0x9f, 0x9f, 0x9f, 0x9f, 0x9f, 0x9f, 0x9f, 0x9f, 0x9f, 0x9f,
    };
    CHECK_EQ(data, expected);
    auto repeated = std::array<u8, 15>{};
    cic.challenge(repeated);
    CHECK_EQ(data, repeated);
    std::array<u8, 15> changed{};
    changed[0] = 0x10;
    cic.challenge(changed);
    CHECK(data != changed);
}

TEST(cic_detection_requires_complete_bootcode) {
    Cic cic;
    cic.configure(CicModel::Nus6106);
    std::vector<u8> truncated(0xfff);
    CHECK(!cic.detect(truncated));
    CHECK_EQ(cic.model(), CicModel::Nus6106);
    CHECK_EQ(Cic::boot_checksum(truncated, 0x85), 0ULL);
}

TEST(cic_unknown_bootcode_does_not_claim_recognition) {
    Cic cic;
    std::vector<u8> invalid(0x1000);
    invalid[0x3e] = 'P';
    CHECK(!cic.detect(invalid));
    CHECK(!cic.recognized());
    CHECK_EQ(cic.model(), CicModel::Nus6102);
    CHECK(cic.pal());
    CHECK(!cic.verify_checksum(std::array<u8, 6>{}));
}

TEST(cic_fixed_region_parts_and_disk_type_are_distinct) {
    Cic cic;
    cic.configure(CicModel::Nus6101, true);
    CHECK(!cic.pal());
    CHECK(!cic.disk_drive());
    cic.configure(CicModel::Nus7102, false);
    CHECK(cic.pal());
    cic.configure(CicModel::Nus8303);
    CHECK(cic.disk_drive());
    CHECK_EQ(cic.seed(), 0xddU);
    cic.configure(CicModel::NusDDUS);
    CHECK_EQ(cic.seed(), 0xdeU);
}
