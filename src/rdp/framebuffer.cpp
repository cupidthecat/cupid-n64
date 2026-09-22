#include "cupid/bus.hpp"

namespace cupid {

u32 Rdp::framebuffer_address(u32 base, unsigned bytes, u32 pixel) const {
    const u32 mask = static_cast<u32>(bus_.rdram.size() - 1U);
    return ((base & ~(bytes - 1U)) + pixel * bytes) & mask;
}

RdpColor Rdp::read_framebuffer_color(u32 address) const {
    const unsigned bytes = color_image_size_ < 2U ? 1U : 1U << (color_image_size_ - 1U);
    const u32 stored = static_cast<u32>(bus_.memory.read(address, bytes));
    if (color_image_size_ == 0U)
        return {0, 0, 0, 224};
    RdpColor color;
    if (color_image_size_ == 1U) {
        const auto intensity = static_cast<s32>(stored);
        return {intensity, intensity, intensity, 224};
    }
    if (color_image_size_ == 2U) {
        if (color_image_format_ == 0U) {
            const unsigned coverage = ((stored & 1U) << 2U) | bus_.memory.hidden_pair(address);
            color = {static_cast<s32>((stored >> 8U) & 248U), static_cast<s32>((stored >> 3U) & 248U),
                     static_cast<s32>((stored << 2U) & 248U), static_cast<s32>(coverage << 5U)};
        } else {
            const auto intensity = static_cast<s32>(stored >> 8U);
            color = {intensity, intensity, intensity, static_cast<s32>(stored & 224U)};
        }
    } else {
        color = rdp_unpack_color(stored);
        color[3] &= 224;
    }
    if ((other_modes_ & (1ULL << 6U)) == 0)
        color[3] = 224;
    return color;
}

void Rdp::write_framebuffer_color(u32 address, const RdpColor& color, unsigned coverage) {
    const auto red = static_cast<u32>(color[0]);
    const auto green = static_cast<u32>(color[1]);
    const auto blue = static_cast<u32>(color[2]);
    if (color_image_size_ < 2U) {
        const u8 hidden = bus_.memory.hidden_pair(address);
        const u32 byte = color_image_size_ == 0U ? 0U : (address & 1U) != 0 ? green : red;
        bus_.memory.write(address, 1, byte);
        const u8 written = static_cast<u8>((byte & 1U) * 3U);
        bus_.memory.set_hidden_pair(address,
                                    color_image_size_ == 0U || (address & 1U) == 0U ? hidden : written);
    } else if (color_image_size_ == 2U) {
        if (color_image_format_ == 0U) {
            bus_.memory.write(address, 2,
                              ((red & 248U) << 8U) | ((green & 248U) << 3U) | ((blue & 248U) >> 2U) |
                                  (coverage >> 2U));
            bus_.memory.set_hidden_pair(address, static_cast<u8>(coverage & 3U));
        } else {
            bus_.memory.write(address, 2, (red << 8U) | (coverage << 5U));
        }
    } else {
        bus_.memory.write(address, 4, (red << 24U) | (green << 16U) | (blue << 8U) | (coverage << 5U));
    }
}

} // namespace cupid
