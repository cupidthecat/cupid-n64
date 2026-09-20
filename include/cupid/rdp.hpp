#pragma once

#include "cupid/rdp/color_pipeline.hpp"
#include "cupid/rdp/texture_coordinates.hpp"
#include "cupid/rdp/tile.hpp"
#include "cupid/types.hpp"

#include <array>

namespace cupid {

class Bus;

class Rdp {
  public:
    explicit Rdp(Bus& bus);

    void reset();
    void tick(u64 rcp_cycles);
    [[nodiscard]] u32 read_register(u32 offset) const;
    void write_register(u32 offset, u32 value);
    [[nodiscard]] u32 read_test_register(u32 offset) const;
    void write_test_register(u32 offset, u32 value);

    [[nodiscard]] u32 start() const {
        return start_;
    }
    [[nodiscard]] u32 end() const {
        return end_;
    }
    [[nodiscard]] u32 current() const {
        return current_;
    }
    [[nodiscard]] bool frozen() const {
        return freeze_;
    }
    [[nodiscard]] const std::array<u8, 4096>& texture_memory() const {
        return texture_memory_;
    }
    [[nodiscard]] const RdpTile& tile(unsigned index) const {
        return tiles_[index & 7U];
    }

  private:
    Bus& bus_;

    u32 start_{};
    u32 end_{};
    u32 current_{};
    u32 clock_{};
    u32 buffer_busy_{};
    u32 pipe_busy_{};
    u32 tmem_busy_{};
    bool source_dmem_{};
    bool freeze_{};
    bool flush_{};
    bool start_valid_{};
    bool end_valid_{};
    bool start_gclk_{};
    bool ready_{true};
    bool crashed_{};

    bool test_check_{};
    bool test_go_{};
    bool test_done_{};
    u8 test_fail_{};
    bool test_enable_{};
    u8 test_address_{};
    std::array<u32, 128> test_data_{};

    u32 color_image_address_{};
    u16 color_image_width_{1};
    u8 color_image_format_{};
    u8 color_image_size_{};
    u32 depth_image_address_{};
    u32 texture_image_address_{};
    u16 texture_image_width_{1};
    u8 texture_image_format_{};
    u8 texture_image_size_{};
    std::array<u8, 4096> texture_memory_{};
    std::array<RdpTile, 8> tiles_{};
    u32 fill_color_{};
    RdpColorState color_state_{};
    u16 primitive_depth_{};
    u16 primitive_delta_depth_{};
    u64 other_modes_{};
    u16 scissor_x0_{};
    u16 scissor_y0_{};
    u16 scissor_x1_{0x0fff};
    u16 scissor_y1_{0x0fff};
    bool scissor_field_enabled_{};
    bool scissor_keep_odd_{};

    std::array<u8, 176> command_buffer_{};
    unsigned command_buffer_size_{};
    u32 command_buffer_address_{};

    void run_commands();
    void halt_commands();
    [[nodiscard]] u8 command_byte(u32 address) const;
    [[nodiscard]] u64 command_word(u32 address) const;
    [[nodiscard]] u64 buffered_word(unsigned offset) const;
    [[nodiscard]] unsigned command_length(u8 opcode) const;
    void execute(u8 opcode);
    void fill_rectangle(u64 command);
    void color_rectangle(u64 command);
    void write_color_pixel(unsigned x, unsigned y, unsigned coverage_mask);
    void fill_span(unsigned y, unsigned left, unsigned right);
    void fill_copy_triangle(bool copy);
    void copy_rectangle(u64 command, bool flipped);
    [[nodiscard]] RdpTextureAttributes triangle_texture_attributes() const;
    void copy_triangle_span(unsigned y, unsigned left, unsigned right,
                            const RdpTextureAttributes& attributes);
    void write_copy_pixel(unsigned x, unsigned y, u16 value);
    [[nodiscard]] u16 copy_texel(const RdpTile& tile, s32 s, s32 t, unsigned lane) const;
    void fill_triangle();
    void set_tile(u64 command);
    void set_tile_size(u64 command);
    void load_texture(u64 command, u8 opcode);
};

} // namespace cupid
