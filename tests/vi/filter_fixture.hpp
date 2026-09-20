#pragma once

#include "cupid/vi/filter.hpp"

#include <array>

namespace test::vi {
using namespace cupid;

struct FilterFixture {
    std::array<u8, 512> bytes{};
    std::array<u8, 256> hidden{};
    std::array<u32, 14> registers{};

    explicit FilterFixture(unsigned format = 3) {
        registers[0] = format;
        registers[2] = 16;
    }
    void put(s32 x, s32 y, ViColor color, u32 coverage = 7) {
        const unsigned size = (registers[0] & 3U) == 3 ? 4 : 2;
        const s32 offset = static_cast<s32>(registers[1] & ~(size - 1U)) +
                           (y * static_cast<s32>(registers[2]) + x) * static_cast<s32>(size);
        const auto address = static_cast<unsigned>((offset % 512 + 512) % 512);
        const u32 word = size == 4 ? (color[0] << 24) | (color[1] << 16) | (color[2] << 8) | (coverage << 5)
                                   : ((color[0] >> 3) << 11) | ((color[1] >> 3) << 6) |
                                         ((color[2] >> 3) << 1) | (coverage >> 2);
        for (unsigned byte = 0; byte < size; ++byte)
            bytes[(address + byte) % bytes.size()] = static_cast<u8>(word >> ((size - byte - 1) * 8));
        hidden[address / 2] = static_cast<u8>(coverage & 3U);
    }
    void gray(s32 x, s32 y, u32 value, u32 coverage = 7) {
        put(x, y, {value, value, value}, coverage);
    }
    [[nodiscard]] ViColor sample(s32 x = 8, s32 y = 3, bool repeat = false) const {
        return ViFilter(bytes, hidden, registers).sample(x, y, repeat);
    }
    void neighbors(const std::array<u32, 6>& values, u32 coverage = 7) {
        constexpr std::array<std::array<s32, 2>, 6> offsets = {
            {{-1, -1}, {1, -1}, {-2, 0}, {2, 0}, {-1, 1}, {1, 1}}};
        for (unsigned index = 0; index < offsets.size(); ++index)
            gray(8 + offsets[index][0], 3 + offsets[index][1], values[index], coverage);
    }
};
} // namespace test::vi
