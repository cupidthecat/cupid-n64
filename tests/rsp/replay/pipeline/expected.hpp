#pragma once

#include <array>
#include <cstdint>

namespace test::rsp_replay::pipeline {
inline constexpr unsigned cases = 1344;
inline constexpr std::uint64_t observations = 8239360ull;
inline constexpr std::array<std::uint64_t, 84> expected{
    0x83743360db541d22ull, 0x5ecb43802d95a7c5ull, 0x721238840afeb49eull, 0xe48861e2466a4ab0ull,
    0x91278bf9cf410463ull, 0xa90e3047e1913de6ull, 0xf0b10bff429d5e73ull, 0x03f7f7a017266a4eull,
    0xfb0151595b17dfaaull, 0x0f6ff404954523dfull, 0x2edbf18067f2c2acull, 0x62ed99aee5d0bf70ull,
    0x57c98b1fdf94cae7ull, 0x74497794cb264e38ull, 0xc6a63de6e0a6db5full, 0x60374d823beee6e3ull,
    0xe26661396d85842cull, 0xa3c4ea6f7c01a4e0ull, 0x0b63481fd22f2b22ull, 0xa1fdab42100f81dcull,
    0xce5c58612d6f2f2eull, 0xfeae7427cbb377bcull, 0xc3477c46662e93c8ull, 0x4bdbd256aad45410ull,
    0x9bd36e61cc477cadull, 0xffb6251f7352e4eeull, 0x685c993747279a3dull, 0x13eb8b2f5830918full,
    0x59a8e1ea06819d31ull, 0xca1a839ba05bb9a8ull, 0x4c110c4e52215b51ull, 0xfe57c4a2c8727c83ull,
    0x10f8646fdc110c7aull, 0x2043a5bcd33a21fdull, 0x8bfbf43cd1990061ull, 0xa3a354912dacbb82ull,
    0x2a2650a01083ba70ull, 0x925c9ca003f6ac59ull, 0x200e40bc05b5aab9ull, 0x568375e438468275ull,
    0x409f79fe5a8e18d0ull, 0x1c020cb3843fc0aaull, 0x86c42c9cc446471bull, 0x22ef2e980f761084ull,
    0x9606bda884983b28ull, 0x1b224c709a651f86ull, 0xbf62d78451d6fcaaull, 0xe5373ed75b0d9050ull,
    0x054a1965ae2ed9c6ull, 0xaf29913d95118bd8ull, 0x5782e930340f1255ull, 0x1e0c86761ce8c577ull,
    0x929cac5ce75f2bacull, 0x01eeca359c33e6e0ull, 0xaea9d2f1920e3e53ull, 0xbbe8e6923317d711ull,
    0x088feb1f40d12eb9ull, 0x010fb40b924bcdf5ull, 0x8f98866d09bdadfdull, 0xfaeb7d3b6b846102ull,
    0xf3939df706e78102ull, 0x379c3dad77959165ull, 0x26dcbc1ae6ffc3d8ull, 0x5f8b78e4e9053096ull,
    0x1bf996080ca1dbb7ull, 0x45dd6be8515b0318ull, 0xb688b36338751235ull, 0x48a5cd19e42a404bull,
    0x6924932344d1b3c2ull, 0x551e20de7ba269b2ull, 0xf1c5d05ea604d73aull, 0xded174e19b83b98bull,
    0x7e8f849ae65bb142ull, 0x3ec56f7482ae4065ull, 0x4308ba20e0691ee8ull, 0xbacd8cd6e92733bcull,
    0xe713819724a98bc9ull, 0x4164a586e1cd0757ull, 0xbcaf97d66e64910cull, 0xcd800957ede62a1bull,
    0xbc51c22c32eb10adull, 0x7e8b88c3af01eec8ull, 0x9c4a8443c1bfdc5bull, 0x6a1a64387aa191d9ull};
} // namespace test::rsp_replay::pipeline
