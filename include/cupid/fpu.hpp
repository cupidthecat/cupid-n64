#pragma once

#include "cupid/types.hpp"

#include <array>

namespace cupid {

class Cpu;

class Fpu {
  public:
    explicit Fpu(Cpu& cpu);

    void reset();
    void execute(u32 instruction);

    [[nodiscard]] u32 read_word(unsigned index) const;
    [[nodiscard]] u64 read_doubleword(unsigned index) const;
    void write_word(unsigned index, u32 value);
    void write_doubleword(unsigned index, u64 value);
    [[nodiscard]] bool compare_nontrapping(u32 instruction) const;
    [[nodiscard]] unsigned nontrapping_arithmetic_cycles(u32 instruction) const;

    std::array<u64, 32> registers{};
    u32 control{};

  private:
    Cpu& cpu_;

    [[nodiscard]] bool full_register_mode() const;
    [[nodiscard]] u32 source_word(unsigned index) const;
    [[nodiscard]] u64 source_doubleword(unsigned index) const;
    [[nodiscard]] u32 second_source_word(unsigned index) const;
    [[nodiscard]] u64 second_source_doubleword(unsigned index) const;
    void write_result_word(unsigned index, u32 value);
    void write_result_doubleword(unsigned index, u64 value);

    void clear_causes();
    void set_control(u32 value);
    [[nodiscard]] u32 read_control(unsigned index) const;
    bool signal_maskable(unsigned flag_index);
    void signal_unimplemented();
    bool handle_host_exceptions(int exceptions, bool conversion);
    bool check_input_word(u32 bits);
    bool check_input_doubleword(u64 bits);
    bool check_inputs_word(u32 left, u32 right);
    bool check_inputs_doubleword(u64 left, u64 right);
    bool finish_word(u32& bits, bool host_underflow = false);
    bool finish_doubleword(u64& bits, bool host_underflow = false);
    void execute_format(u32 instruction, unsigned format);
    void execute_compare(u32 instruction, unsigned format);
};

} // namespace cupid
