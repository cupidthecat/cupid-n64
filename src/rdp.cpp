#include "cupid/bus.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <vector>

namespace cupid {
namespace {

constexpr u32 address_mask = 0x00fffff8U;

s32 sign_extend14(u16 value) {
    const u32 raw = static_cast<u32>(value) & 0x3fffU;
    return static_cast<s32>((raw ^ 0x2000U) - 0x2000U);
}

u8 coverage_alpha(unsigned coverage, unsigned mode) {
    if (mode == 2U || mode == 1U)
        return 0xe0U;
    if (mode == 3U)
        return static_cast<u8>(coverage);
    switch (coverage) {
    case 4:
        return 0x20;
    case 7:
        return 0x40;
    case 8:
    case 9:
    case 10:
        return 0x60;
    case 12:
        return 0xa0;
    case 16:
        return 0xe0;
    default:
        return static_cast<u8>(coverage);
    }
}

u32 status_word(bool source_dmem, bool freeze, bool flush, bool start_gclk, u32 tmem_busy, u32 pipe_busy,
                u32 buffer_busy, bool ready, bool end_valid, bool start_valid, bool crashed) {
    u32 value = 0;
    if (source_dmem)
        value |= 1U << 0;
    if (freeze || crashed)
        value |= 1U << 1;
    if (flush)
        value |= 1U << 2;
    if (start_gclk)
        value |= 1U << 3;
    if (tmem_busy != 0)
        value |= 1U << 4;
    if (pipe_busy != 0)
        value |= 1U << 5;
    if (buffer_busy != 0)
        value |= 1U << 6;
    if (ready)
        value |= 1U << 7;
    if (end_valid)
        value |= 1U << 9;
    if (start_valid)
        value |= 1U << 10;
    return value;
}

} // namespace

Rdp::Rdp(Bus& bus) : bus_(bus) {}

void Rdp::reset() {
    start_ = 0;
    end_ = 0;
    current_ = 0;
    clock_ = 0;
    buffer_busy_ = 0;
    pipe_busy_ = 0;
    tmem_busy_ = 0;
    source_dmem_ = false;
    freeze_ = false;
    flush_ = false;
    start_valid_ = false;
    end_valid_ = false;
    start_gclk_ = false;
    ready_ = true;
    crashed_ = false;
    test_check_ = false;
    test_go_ = false;
    test_done_ = false;
    test_fail_ = 0;
    test_enable_ = false;
    test_address_ = 0;
    test_data_.fill(0);
    color_image_address_ = 0;
    color_image_width_ = 1;
    color_image_format_ = 0;
    color_image_size_ = 0;
    depth_image_address_ = 0;
    texture_image_address_ = 0;
    texture_image_width_ = 1;
    texture_image_format_ = 0;
    texture_image_size_ = 0;
    texture_memory_.fill(0);
    tiles_.fill({});
    fill_color_ = 0;
    color_state_ = {};
    primitive_depth_ = 0;
    primitive_delta_depth_ = 0;
    other_modes_ = 0;
    scissor_x0_ = 0;
    scissor_y0_ = 0;
    scissor_x1_ = 0x0fff;
    scissor_y1_ = 0x0fff;
    scissor_field_enabled_ = false;
    scissor_keep_odd_ = false;
    command_buffer_.fill(0);
    command_buffer_size_ = 0;
    command_buffer_address_ = 0;
    bus_.set_interrupt(5, false);
}

void Rdp::tick(u64 rcp_cycles) {
    clock_ = (clock_ + static_cast<u32>(rcp_cycles & 0x00ffffffU)) & 0x00ffffffU;
}

u32 Rdp::read_register(u32 offset) const {
    switch ((offset & 0x1fU) >> 2U) {
    case 0:
        return start_ & 0x00ffffffU;
    case 1:
        return end_ & 0x00ffffffU;
    case 2:
        return current_ & 0x00ffffffU;
    case 3:
        return status_word(source_dmem_, freeze_, flush_, start_gclk_, tmem_busy_, pipe_busy_, buffer_busy_,
                           ready_, end_valid_, start_valid_, crashed_);
    case 4:
        return clock_ & 0x00ffffffU;
    case 5:
        return buffer_busy_ & 0x00ffffffU;
    case 6:
        return pipe_busy_ & 0x00ffffffU;
    case 7:
        return tmem_busy_ & 0x00ffffffU;
    default:
        return 0;
    }
}

void Rdp::write_register(u32 offset, u32 value) {
    switch ((offset & 0x1fU) >> 2U) {
    case 0:
        if (!start_valid_)
            start_ = value & address_mask;
        start_valid_ = true;
        return;
    case 1:
        end_ = value & address_mask;
        if (start_valid_) {
            current_ = start_;
            command_buffer_size_ = 0;
            command_buffer_address_ = current_;
            start_valid_ = false;
        }
        run_commands();
        return;
    case 3:
        if ((value & (1U << 0)) != 0)
            source_dmem_ = false;
        if ((value & (1U << 1)) != 0)
            source_dmem_ = true;
        if ((value & (1U << 2)) != 0) {
            freeze_ = false;
            run_commands();
        }
        if ((value & (1U << 3)) != 0)
            freeze_ = true;
        if ((value & (1U << 4)) != 0)
            flush_ = false;
        if ((value & (1U << 5)) != 0) {
            flush_ = true;
            command_buffer_size_ = 0;
            command_buffer_address_ = current_;
        }
        if ((value & (1U << 6)) != 0 && !crashed_)
            tmem_busy_ = 0;
        if ((value & (1U << 7)) != 0 && !crashed_)
            pipe_busy_ = 0;
        if ((value & (1U << 8)) != 0 && !crashed_)
            buffer_busy_ = 0;
        if ((value & (1U << 9)) != 0)
            clock_ = 0;
        return;
    default:
        return;
    }
}

u32 Rdp::read_test_register(u32 offset) const {
    switch ((offset & 0x0fU) >> 2U) {
    case 0:
        return static_cast<u32>(test_check_) | (static_cast<u32>(test_go_) << 1U) |
               (static_cast<u32>(test_done_) << 2U) | (static_cast<u32>(test_fail_) << 3U);
    case 1:
        return static_cast<u32>(test_enable_);
    case 2:
        return test_address_ & 0x7fU;
    case 3:
        return test_data_[test_address_ & 0x7fU];
    default:
        return 0;
    }
}

void Rdp::write_test_register(u32 offset, u32 value) {
    switch ((offset & 0x0fU) >> 2U) {
    case 0:
        test_check_ = (value & 1U) != 0;
        test_go_ = (value & 2U) != 0;
        if ((value & 4U) != 0)
            test_done_ = false;
        return;
    case 1:
        test_enable_ = (value & 1U) != 0;
        return;
    case 2:
        test_address_ = static_cast<u8>(value & 0x7fU);
        return;
    case 3: {
        u32 stored = value;
        const unsigned column = test_address_ & 3U;
        if (column == 2U)
            stored &= 0xffU;
        else if (column == 3U)
            stored = 0;
        test_data_[test_address_ & 0x7fU] = stored;
        return;
    }
    default:
        return;
    }
}

u8 Rdp::command_byte(u32 address) const {
    return bus_.rdp_source_byte(address, source_dmem_);
}

u64 Rdp::command_word(u32 address) const {
    u64 value = 0;
    for (unsigned byte = 0; byte < 8; ++byte) {
        value = (value << 8U) | command_byte(address + byte);
    }
    return value;
}

u64 Rdp::buffered_word(unsigned offset) const {
    u64 value = 0;
    for (unsigned byte = 0; byte < 8; ++byte) {
        value = (value << 8U) | command_buffer_[offset + byte];
    }
    return value;
}

unsigned Rdp::command_length(u8 opcode) const {
    switch (opcode & 0x3fU) {
    case 0x08:
        return 32;
    case 0x09:
        return 48;
    case 0x0a:
        return 96;
    case 0x0b:
        return 112;
    case 0x0c:
        return 96;
    case 0x0d:
        return 112;
    case 0x0e:
        return 160;
    case 0x0f:
        return 176;
    case 0x24:
    case 0x25:
        return 16;
    default:
        return 8;
    }
}

void Rdp::halt_commands() {
    crashed_ = true;
    pipe_busy_ = 1;
    buffer_busy_ = 1;
}

void Rdp::run_commands() {
    if (freeze_ || crashed_)
        return;
    buffer_busy_ = 1;
    pipe_busy_ = 1;
    start_gclk_ = true;

    while (current_ < end_) {
        if (command_buffer_size_ + 8U > command_buffer_.size()) {
            halt_commands();
            return;
        }
        if (command_buffer_size_ == 0)
            command_buffer_address_ = current_;
        for (unsigned byte = 0; byte < 8; ++byte) {
            command_buffer_[command_buffer_size_ + byte] = command_byte(current_ + byte);
        }
        command_buffer_size_ += 8;
        current_ += 8;

        for (;;) {
            if (command_buffer_size_ < 8)
                break;
            const u8 opcode = static_cast<u8>((buffered_word(0) >> 56) & 0x3fU);
            const unsigned length = command_length(opcode);
            if (command_buffer_size_ < length)
                break;
            execute(opcode);
            if (crashed_)
                return;
            const unsigned remaining = command_buffer_size_ - length;
            std::move(command_buffer_.begin() + static_cast<std::ptrdiff_t>(length),
                      command_buffer_.begin() + static_cast<std::ptrdiff_t>(command_buffer_size_),
                      command_buffer_.begin());
            command_buffer_size_ = remaining;
            command_buffer_address_ += length;
        }
    }

    buffer_busy_ = 0;
    ready_ = true;
}

void Rdp::execute(u8 opcode) {
    const u64 command = buffered_word(0);
    const bool draw =
        (opcode >= 0x08 && opcode <= 0x0f) || opcode == 0x24 || opcode == 0x25 || opcode == 0x36;
    const unsigned cycle = static_cast<unsigned>((other_modes_ >> 52U) & 3U);
    if (draw && ((cycle == 3U && (color_image_size_ == 0U || (other_modes_ & 0x50U) != 0 ||
                                  ((other_modes_ & 0x20U) != 0 && (other_modes_ & 4U) == 0))) ||
                 (cycle == 2U && color_image_size_ == 3U))) {
        halt_commands();
        return;
    }
    switch (opcode & 0x3fU) {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1a:
    case 0x1b:
    case 0x1c:
    case 0x1d:
    case 0x1e:
    case 0x1f:
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x31:
        return;
    case 0x26:
    case 0x27:
    case 0x28:
        return;
    case 0x08:
    case 0x09:
    case 0x0a:
    case 0x0b:
    case 0x0c:
    case 0x0d:
    case 0x0e:
    case 0x0f:
        if (cycle == 3U || cycle == 2U)
            fill_copy_triangle(cycle == 2U);
        else if (opcode == 0x08)
            fill_triangle();
        return;
    case 0x24:
    case 0x25:
    case 0x36:
        if (((other_modes_ >> 52U) & 3U) == 3U)
            fill_rectangle(command);
        else if (cycle < 2U)
            color_rectangle(command, opcode == 0x25);
        else
            copy_rectangle(command, opcode == 0x25);
        return;
    case 0x29:
        bus_.set_interrupt(5, true);
        buffer_busy_ = 0;
        pipe_busy_ = 0;
        start_gclk_ = false;
        return;
    case 0x2a:
        color_state_.key_width[1] = static_cast<u16>((command >> 44U) & 4095U);
        color_state_.key_width[2] = static_cast<u16>((command >> 32U) & 4095U);
        color_state_.key_center[1] = static_cast<u8>(command >> 24U);
        color_state_.key_scale[1] = static_cast<u8>(command >> 16U);
        color_state_.key_center[2] = static_cast<u8>(command >> 8U);
        color_state_.key_scale[2] = static_cast<u8>(command);
        return;
    case 0x2b:
        color_state_.key_width[0] = static_cast<u16>((command >> 16U) & 4095U);
        color_state_.key_center[0] = static_cast<u8>(command >> 8U);
        color_state_.key_scale[0] = static_cast<u8>(command);
        return;
    case 0x2c:
        for (unsigned index = 0; index < 6; ++index)
            color_state_.convert[index] = static_cast<u16>((command >> ((5U - index) * 9U)) & 511U);
        return;
    case 0x2d:
        scissor_x0_ = static_cast<u16>((command >> 44) & 0x0fffU);
        scissor_y0_ = static_cast<u16>((command >> 32) & 0x0fffU);
        scissor_x1_ = static_cast<u16>((command >> 12) & 0x0fffU);
        scissor_y1_ = static_cast<u16>(command & 0x0fffU);
        scissor_field_enabled_ = (command & (1ULL << 25U)) != 0;
        scissor_keep_odd_ = (command & (1ULL << 24U)) != 0;
        return;
    case 0x2e:
        primitive_depth_ = static_cast<u16>(command >> 16U);
        primitive_delta_depth_ = static_cast<u16>(command);
        return;
    case 0x2f:
        other_modes_ = command & 0x00ffffffffffffffULL;
        return;
    case 0x30:
    case 0x33:
    case 0x34:
        load_texture(command, opcode);
        return;
    case 0x32:
        set_tile_size(command);
        return;
    case 0x35:
        set_tile(command);
        return;
    case 0x37:
        fill_color_ = static_cast<u32>(command);
        return;
    case 0x38:
        color_state_.fog = static_cast<u32>(command);
        return;
    case 0x39:
        color_state_.blend = static_cast<u32>(command);
        return;
    case 0x3a:
        color_state_.primitive = static_cast<u32>(command);
        color_state_.minimum_lod = static_cast<u8>((command >> 40U) & 31U);
        color_state_.primitive_lod = static_cast<u8>(command >> 32U);
        return;
    case 0x3b:
        color_state_.environment = static_cast<u32>(command);
        return;
    case 0x3c:
        color_state_.combine = command & 0x00ffffffffffffffULL;
        return;
    case 0x3d:
        texture_image_format_ = static_cast<u8>((command >> 53) & 7U);
        texture_image_size_ = static_cast<u8>((command >> 51) & 3U);
        texture_image_width_ = static_cast<u16>(((command >> 32) & 0x03ffU) + 1U);
        texture_image_address_ = static_cast<u32>(command & 0x00ffffffU);
        return;
    case 0x3e:
        depth_image_address_ = static_cast<u32>(command & 0x03ffffffU);
        return;
    case 0x3f:
        color_image_format_ = static_cast<u8>((command >> 53) & 7U);
        color_image_size_ = static_cast<u8>((command >> 51) & 3U);
        color_image_width_ = static_cast<u16>(((command >> 32) & 0x03ffU) + 1U);
        color_image_address_ = static_cast<u32>(command & 0x00ffffffU);
        return;
    default:
        return;
    }
}

void Rdp::fill_triangle() {
    if (((other_modes_ >> 52U) & 3U) != 0U)
        return;
    if (color_image_format_ != 0U || (color_image_size_ != 2U && color_image_size_ != 3U))
        return;

    const u64 edge = buffered_word(0);
    const bool right_major = ((edge >> 55U) & 1U) != 0;
    const s32 yl = sign_extend14(static_cast<u16>(edge >> 32U));
    const s32 ym = sign_extend14(static_cast<u16>(edge >> 16U));
    const s32 yh = sign_extend14(static_cast<u16>(edge));
    const u64 low = buffered_word(8);
    const u64 high = buffered_word(16);
    const u64 middle = buffered_word(24);
    const s64 xl = static_cast<s64>(std::bit_cast<s32>(static_cast<u32>(low >> 32U))) << 2U;
    const s64 dl = std::bit_cast<s32>(static_cast<u32>(low));
    const s64 xh = static_cast<s64>(std::bit_cast<s32>(static_cast<u32>(high >> 32U))) << 2U;
    const s64 dh = std::bit_cast<s32>(static_cast<u32>(high));
    const s64 xm = static_cast<s64>(std::bit_cast<s32>(static_cast<u32>(middle >> 32U))) << 2U;
    const s64 dm = std::bit_cast<s32>(static_cast<u32>(middle));

    const unsigned width = color_image_width_;
    const unsigned height = std::min(1024U, std::max(1U, (static_cast<unsigned>(scissor_y1_) + 3U) >> 2U));
    std::vector<u8> coverage(static_cast<std::size_t>(width) * height, 0);
    s64 major_x = xh;
    s32 y = yh;

    const auto section = [&](s32 target_y, s64 initial_minor, s64 minor_increment, s32& current_y,
                             s64& current_major) {
        s64 minor_x = initial_minor;
        while (current_y < target_y) {
            if ((current_y >> 2) >= ((static_cast<s32>(scissor_y0_) + 3) >> 2) &&
                current_y < static_cast<s32>(scissor_y1_)) {
                const s64 left = right_major ? current_major : minor_x;
                const s64 right = right_major ? minor_x : current_major;
                if (right >= left) {
                    const s64 subpixel_left = left >> 16;
                    const s64 subpixel_right = (right - 8) >> 16;
                    for (s64 x = subpixel_left; x <= subpixel_right; ++x) {
                        if ((x >> 2) < ((static_cast<s64>(scissor_x0_) + 3) >> 2))
                            continue;
                        if (x >= static_cast<s64>(scissor_x1_))
                            break;
                        const s64 pixel_x = x >> 2;
                        const s64 pixel_y = static_cast<s64>(current_y) >> 2;
                        if (pixel_x < 0 || pixel_y < 0 || pixel_x >= static_cast<s64>(width) ||
                            pixel_y >= static_cast<s64>(height))
                            continue;
                        u8& count = coverage[static_cast<std::size_t>(pixel_y) * width +
                                             static_cast<std::size_t>(pixel_x)];
                        if (count != 0xffU)
                            ++count;
                    }
                }
            }
            ++current_y;
            current_major += dh;
            minor_x += minor_increment;
        }
    };

    section(ym, xm, dm, y, major_x);
    section(yl, xl, dl, y, major_x);

    const unsigned coverage_mode = static_cast<unsigned>((other_modes_ >> 8U) & 3U);
    const u8 red = static_cast<u8>(color_state_.blend >> 16U);
    const u8 green = static_cast<u8>(color_state_.blend >> 8U);
    const u8 blue = static_cast<u8>(color_state_.blend);
    for (unsigned pixel_y = 0; pixel_y < height; ++pixel_y) {
        if (scissor_field_enabled_ && (pixel_y & 1U) != static_cast<unsigned>(scissor_keep_odd_))
            continue;
        for (unsigned pixel_x = 0; pixel_x < width; ++pixel_x) {
            const unsigned count = coverage[static_cast<std::size_t>(pixel_y) * width + pixel_x];
            if (count == 0)
                continue;
            const u8 alpha = coverage_alpha(count, coverage_mode);
            const u32 pixel = pixel_y * width + pixel_x;
            if (color_image_size_ == 2U) {
                const u32 color_bits = (static_cast<u32>(red >> 3U) << 11U) |
                                       (static_cast<u32>(green >> 3U) << 6U) |
                                       (static_cast<u32>(blue >> 3U) << 1U) | (alpha > 127U ? 1U : 0U);
                const u16 color = static_cast<u16>(color_bits);
                bus_.memory.write(color_image_address_ + pixel * 2U, 2, color);
            } else {
                const u32 color = (static_cast<u32>(alpha) << 24U) | (static_cast<u32>(red) << 16U) |
                                  (static_cast<u32>(green) << 8U) | blue;
                bus_.memory.write(color_image_address_ + pixel * 4U, 4, color);
            }
        }
    }
}

} // namespace cupid
