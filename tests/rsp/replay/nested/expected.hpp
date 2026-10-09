#pragma once

#include <array>
#include <cstdint>

namespace test::rsp_replay::nested {
inline constexpr unsigned cases = 2592;
inline constexpr std::uint64_t observations = 14878080ull;
inline constexpr std::array<std::uint64_t, 162> expected{
    0xce851f3919b8284cull, 0x5213aea379b57143ull, 0x43a316fdae3ce089ull, 0x20dcc4745c15b72cull,
    0xb2424966925fe147ull, 0xfb0cae732de4df87ull, 0x50f36bc8c8c3c5d8ull, 0x7d0e7f48f63a8866ull,
    0x98789a7aac55a5aaull, 0x1e4c559cc9664ad4ull, 0xf8f055d92d5cef16ull, 0x032cdd4983a7a7b7ull,
    0x34dbc67babd3ab60ull, 0xcac2d955a34c2bbcull, 0x04a5df726094bc78ull, 0x365cd759dae0fd14ull,
    0xd7551270e77a73e3ull, 0x373417bb59269398ull, 0xdd0a57d518865f7full, 0x461a86233f1ec5f0ull,
    0x8c9065b2f8248470ull, 0xb3bc8f17f49b6d82ull, 0x49c02b45ee75b97bull, 0xcd5510aafcccee59ull,
    0x62262fd6982a9e47ull, 0xe660c4f9a60c70c9ull, 0x3aac8c9d2624a00cull, 0xe19e52bab1b7fad1ull,
    0x0b5221bc5d752b74ull, 0x36dbace873c4d09cull, 0x5649bd389a33eddfull, 0x2c9fb00715af464aull,
    0x1cb3a20a15c57812ull, 0x7dac198ee37b05a6ull, 0xcba3fcd17519a23eull, 0xd6684a2e7646d653ull,
    0x3517010451129dbcull, 0x3af87dc9013334d5ull, 0xeebab0ebdb81da6dull, 0xd717af2039986003ull,
    0xcb60a41f70d44156ull, 0x2c703ea71b5d1e57ull, 0x3600966558237db9ull, 0x4d09b048d989f28bull,
    0x90731fb3b9fa54c2ull, 0x5f9aca338dcb6c45ull, 0xb586f903ce991740ull, 0xc18d46a7cd2e9595ull,
    0xfdba443d716553c0ull, 0x27d8d79fa3c82e23ull, 0xf5b0bfd43aa64738ull, 0x9746c35c33092a1full,
    0x47721428a03a3bc9ull, 0x7321e26d1202ae81ull, 0x491ed9fc6f9a384full, 0x946e7b4303e5ea73ull,
    0xa3b4864ebe0ba582ull, 0x42906393d8747325ull, 0xbe590d66e9d4e69bull, 0x204f83d5566ffb62ull,
    0xba9279e08a004c90ull, 0x3101b773173ada63ull, 0x2a0dd0129d0ff6e2ull, 0x49226e9641bba5c2ull,
    0xb4315a1fb73bf352ull, 0x3d62db0a9a5d9695ull, 0xbcdf54bb24e69dedull, 0xd95df40004b8436full,
    0x7f97451baf1c0ef6ull, 0x796da9e64f510e34ull, 0x45e6499516258abcull, 0xeb755426d7daf8b9ull,
    0x8d00219d35538e3aull, 0x6d4a56213cc4ade0ull, 0xbb4f0f4ee42eaea5ull, 0x4eef608882b5a4c1ull,
    0x3af999e2c70f27a2ull, 0x954fc9f8b7ed000eull, 0xc6cd3e8723ce13b4ull, 0x35f9096a8df97b51ull,
    0xef1bad23de623c48ull, 0x6801d857084ba4aeull, 0xa51db5c6ca52ad3dull, 0x905d69d6b481ef45ull,
    0xdd9b30972597017aull, 0x44ebb717e882d1cfull, 0x59e01de56319ff1eull, 0x4ae5d86c22a1699eull,
    0xeb62b4293c0c8fc9ull, 0x0d4086f3b64bc1dbull, 0x47a7e35da3decb6dull, 0x9b7ca06dcef91a1bull,
    0xcf158caae98e2f28ull, 0x5e24fc06d3fdb943ull, 0xadacbb0f7f90e7bdull, 0xb817a10b1a3ffde0ull,
    0xd23d858291ddbf5full, 0xc82025e08e89b60eull, 0x0509efabcf27b75bull, 0x6a73b50da312e42full,
    0x2a56f66f3e18c35eull, 0x694f51d9f4e6be2dull, 0xf641f8b2731a971dull, 0x79748d0e57de7b77ull,
    0x2ff181e631360faeull, 0x3a7d98b8cef41871ull, 0x64d2f7f7a05c60bbull, 0xbecdba235c331014ull,
    0xf1b7020ef794d70full, 0xe87c9aab4fc1babdull, 0x411247165f95861eull, 0x7f9d84752f85b798ull,
    0xc7f9a019288e7972ull, 0x87e6f0977cd5c9cfull, 0x2b3a9f481fffaa7eull, 0xc8f2a8e08854ca72ull,
    0xd316a86b61b94392ull, 0x9f92f6d4afa18724ull, 0x6afb5b4178d1007eull, 0x41ed8ccacab9cfcbull,
    0x5fb5e2c44dd04b1bull, 0x768373265cbb23c3ull, 0x9ebe44fd30e04d32ull, 0x9a3ed07deb519e5eull,
    0x83c6fa73a10b317dull, 0x0d560cd8a78950bcull, 0xf280fad434b2430full, 0x409557fec94f7334ull,
    0xc868cb2e52876310ull, 0xc824b3049158ae1bull, 0x300b73e4ee68b2fbull, 0xdd8b254843fc0571ull,
    0x891402f50c773925ull, 0xcce90f5ce4dd1210ull, 0xe9901297de75fe8eull, 0x76aa4f02c4fb0b97ull,
    0x45483b3bebcb0ec3ull, 0xb99561cdaa5e2ce6ull, 0x95915cae0b4cbc59ull, 0x69a166130eae0889ull,
    0x6d1663f4a8b8b821ull, 0xfe0648575fe2bfd2ull, 0xd796fa611a55c4aeull, 0xb99a43374f4fb187ull,
    0x5d3b188e16eb4fbeull, 0xce84136e88aa09d9ull, 0x558f8ed02fe06419ull, 0xaa8d1fab657f8c6aull,
    0xc8617d46036a8d9dull, 0x5542587afae8e4b8ull, 0xf3d199e97ffa25b7ull, 0xe132d3ef31009379ull,
    0xea26936c5fff80d5ull, 0xfee725351a2f9772ull, 0xa65c933bc4da8b67ull, 0xcb06f5e5d7a57e5aull,
    0x6c0b7918baf36fbaull, 0xa6aeadc009074c3aull, 0xb01ccf3cf718f6a0ull, 0xbc8c11d7d527e3bdull,
    0xe4e6f9b81809c715ull, 0x654bdd53684d4cfcull};
} // namespace test::rsp_replay::nested
