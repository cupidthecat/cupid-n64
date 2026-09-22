#pragma once

#include "cupid/types.hpp"

#include <array>
#include <span>

namespace cupid {

using ViColor = std::array<u32, 3>;

struct ViPixel {
    ViColor color{};
    u32 coverage{};
};

class ViFilter {
  public:
    ViFilter(std::span<const u8> bytes, std::span<const u8> hidden, std::span<const u32, 14> registers);
    [[nodiscard]] ViColor sample(s32 x, s32 y, bool repeat_lower = false) const;
    void sample_row(s32 x, s32 y, bool repeat_lower, std::span<ViColor> output) const;

  private:
    std::span<const u8> bytes_;
    std::span<const u8> hidden_;
    u32 control_;
    u32 origin_;
    u32 stride_;
    unsigned pixel_bytes_;
    std::size_t address_mask_;
    bool power_of_two_size_;
    [[nodiscard]] ViPixel fetch(s32 x, s32 y) const;
    [[nodiscard]] ViPixel reconstruct(s32 x, s32 y, bool repeat_lower) const;
};

[[nodiscard]] ViColor vi_gamma(ViColor color, bool gamma, bool dither, u16 noise);
[[nodiscard]] u16 vi_gamma_noise(u32 field, unsigned x, unsigned y);

} // namespace cupid
