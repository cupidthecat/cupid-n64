#pragma once

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
    u32 fill_color_{};
    u32 blend_color_{};
    u64 other_modes_{};
    u16 scissor_x0_{};
    u16 scissor_y0_{};
    u16 scissor_x1_{0x0fff};
    u16 scissor_y1_{0x0fff};

    std::array<u8, 176> command_buffer_{};
    unsigned command_buffer_size_{};
    u32 command_buffer_address_{};

    void run_commands();
    [[nodiscard]] u8 command_byte(u32 address) const;
    [[nodiscard]] u64 command_word(u32 address) const;
    [[nodiscard]] u64 buffered_word(unsigned offset) const;
    [[nodiscard]] unsigned command_length(u8 opcode) const;
    void execute(u8 opcode);
    void fill_rectangle(u64 command);
    void fill_triangle();
};

} // namespace cupid
