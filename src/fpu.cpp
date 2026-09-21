#include "cupid/system.hpp"

#include <bit>
#include <cfenv>
#include <cmath>
#include <limits>
#include <type_traits>

namespace cupid {
namespace {

constexpr u32 control_writable_mask = 0x0183ffffU;
constexpr u32 cause_mask = 0x0003f000U;

template <class T> struct FloatBits;

template <> struct FloatBits<float> {
    using UInt = u32;
    static constexpr UInt exponent_mask = 0x7f800000U;
    static constexpr UInt fraction_mask = 0x007fffffU;
    static constexpr UInt nan_invalid_bit = 0x00400000U;
    static constexpr UInt sign_mask = 0x80000000U;
    static constexpr UInt minimum_normal = 0x00800000U;
    static constexpr UInt canonical_nan = 0x7fbfffffU;
};

template <> struct FloatBits<double> {
    using UInt = u64;
    static constexpr UInt exponent_mask = 0x7ff0000000000000ULL;
    static constexpr UInt fraction_mask = 0x000fffffffffffffULL;
    static constexpr UInt nan_invalid_bit = 0x0008000000000000ULL;
    static constexpr UInt sign_mask = 0x8000000000000000ULL;
    static constexpr UInt minimum_normal = 0x0010000000000000ULL;
    static constexpr UInt canonical_nan = 0x7ff7ffffffffffffULL;
};

template <class T> bool is_nan_bits(typename FloatBits<T>::UInt bits) {
    return (bits & FloatBits<T>::exponent_mask) == FloatBits<T>::exponent_mask &&
           (bits & FloatBits<T>::fraction_mask) != 0;
}

template <class T> bool is_infinite_bits(typename FloatBits<T>::UInt bits) {
    return (bits & ~FloatBits<T>::sign_mask) == FloatBits<T>::exponent_mask;
}

template <class T> bool is_subnormal_bits(typename FloatBits<T>::UInt bits) {
    return (bits & FloatBits<T>::exponent_mask) == 0 && (bits & FloatBits<T>::fraction_mask) != 0;
}

template <class T> bool nan_raises_invalid(typename FloatBits<T>::UInt bits) {
    return (bits & FloatBits<T>::nan_invalid_bit) != 0;
}

template <class T> bool arithmetic_shortcut(typename FloatBits<T>::UInt bits) {
    return (bits & ~FloatBits<T>::sign_mask) == 0 ||
           (bits & FloatBits<T>::exponent_mask) == FloatBits<T>::exponent_mask;
}

template <class T>
u64 arithmetic_latency(unsigned function, typename FloatBits<T>::UInt left, typename FloatBits<T>::UInt right,
                       bool flush_underflow) {
    if (arithmetic_shortcut<T>(left) || arithmetic_shortcut<T>(right))
        return 1;
    if (function == 2 && ((left & FloatBits<T>::fraction_mask) == 0 ||
                          (right & FloatBits<T>::fraction_mask) == 0 || flush_underflow))
        return 1;
    if (function < 2)
        return 2;
    if (function == 2)
        return sizeof(T) == 4 ? 4 : 7;
    return sizeof(T) == 4 ? 28 : 57;
}

int host_rounding(u32 mode) {
    switch (mode & 3U) {
    case 0:
        return FE_TONEAREST;
    case 1:
        return FE_TOWARDZERO;
    case 2:
        return FE_UPWARD;
    default:
        return FE_DOWNWARD;
    }
}

class ScopedEnvironment {
  public:
    explicit ScopedEnvironment(u32 mode)
        : saved_(std::fegetenv(&environment_) == 0), previous_(std::fegetround()) {
        // FCSR controls guest flushing and traps independently of the calling thread.
        if (saved_)
            std::fesetenv(FE_DFL_ENV);
        std::fesetround(host_rounding(mode));
    }

    ~ScopedEnvironment() {
        if (saved_)
            std::fesetenv(&environment_);
        else if (previous_ != -1)
            std::fesetround(previous_);
    }

  private:
    fenv_t environment_{};
    bool saved_{};
    int previous_;
};

template <class T> T round_integral(T value, u32 mode) {
    switch (mode & 3U) {
    case 0: {
        ScopedEnvironment environment(0);
        return std::nearbyint(value);
    }
    case 1:
        return std::trunc(value);
    case 2:
        return std::ceil(value);
    default:
        return std::floor(value);
    }
}

template <class T> typename FloatBits<T>::UInt bits_of(T value) {
    return std::bit_cast<typename FloatBits<T>::UInt>(value);
}

template <class T> T value_of(typename FloatBits<T>::UInt bits) {
    return std::bit_cast<T>(bits);
}

} // namespace

Fpu::Fpu(Cpu& cpu) : cpu_(cpu) {}

void Fpu::reset() {
    registers.fill(0);
    control = 0;
}

bool Fpu::full_register_mode() const {
    return (cpu_.status() & 0x04000000U) != 0;
}

u32 Fpu::read_word(unsigned index) const {
    index &= 31U;
    if (full_register_mode())
        return static_cast<u32>(registers[index]);
    const u64 value = registers[index & ~1U];
    return (index & 1U) != 0 ? static_cast<u32>(value >> 32) : static_cast<u32>(value);
}

u64 Fpu::read_doubleword(unsigned index) const {
    index &= 31U;
    return registers[full_register_mode() ? index : (index & ~1U)];
}

void Fpu::write_word(unsigned index, u32 value) {
    index &= 31U;
    if (full_register_mode()) {
        registers[index] = (registers[index] & 0xffffffff00000000ULL) | value;
        return;
    }
    u64& target = registers[index & ~1U];
    if ((index & 1U) != 0)
        target = (target & 0x00000000ffffffffULL) | (static_cast<u64>(value) << 32);
    else
        target = (target & 0xffffffff00000000ULL) | value;
}

void Fpu::write_doubleword(unsigned index, u64 value) {
    index &= 31U;
    registers[full_register_mode() ? index : (index & ~1U)] = value;
}

u32 Fpu::source_word(unsigned index) const {
    index &= 31U;
    return static_cast<u32>(registers[full_register_mode() ? index : (index & ~1U)]);
}

u64 Fpu::source_doubleword(unsigned index) const {
    index &= 31U;
    return registers[full_register_mode() ? index : (index & ~1U)];
}

u32 Fpu::second_source_word(unsigned index) const {
    return static_cast<u32>(registers[index & 31U]);
}

u64 Fpu::second_source_doubleword(unsigned index) const {
    return registers[index & 31U];
}

void Fpu::write_result_word(unsigned index, u32 value) {
    registers[index & 31U] = value;
}

void Fpu::write_result_doubleword(unsigned index, u64 value) {
    registers[index & 31U] = value;
}

void Fpu::clear_causes() {
    control &= ~cause_mask;
}

u32 Fpu::read_control(unsigned index) const {
    if ((index & 31U) == 0)
        return 0x00000a00U;
    if ((index & 31U) == 31)
        return control;
    return 0;
}

void Fpu::set_control(u32 value) {
    control = value & control_writable_mask;
    const u32 causes = (control >> 12) & 0x1fU;
    const u32 enables = (control >> 7) & 0x1fU;
    if ((control & (1U << 17)) != 0 || (causes & enables) != 0) {
        cpu_.raise_exception(Exception::FloatingPoint);
    }
}

bool Fpu::signal_maskable(unsigned flag_index) {
    const u32 bit = 1U << (flag_index & 7U);
    control |= bit << 12;
    if ((control & (bit << 7)) != 0)
        return true;
    control |= bit << 2;
    return false;
}

void Fpu::signal_unimplemented() {
    control |= 1U << 17;
    cpu_.raise_exception(Exception::FloatingPoint);
}

bool Fpu::handle_host_exceptions(int exceptions, bool conversion) {
    if (conversion && (exceptions & FE_INVALID) != 0) {
        signal_unimplemented();
        return false;
    }
    if ((exceptions & FE_UNDERFLOW) != 0) {
        const bool flush = (control & (1U << 24)) != 0;
        const bool underflow_enabled = (control & (1U << 8)) != 0;
        const bool inexact_enabled = (control & (1U << 7)) != 0;
        if (!flush || underflow_enabled || inexact_enabled) {
            signal_unimplemented();
            return false;
        }
    }

    bool trap = false;
    if ((exceptions & FE_DIVBYZERO) != 0)
        trap |= signal_maskable(3);
    if ((exceptions & FE_INEXACT) != 0)
        trap |= signal_maskable(0);
    if ((exceptions & FE_UNDERFLOW) != 0)
        trap |= signal_maskable(1);
    if ((exceptions & FE_OVERFLOW) != 0)
        trap |= signal_maskable(2);
    if ((exceptions & FE_INVALID) != 0)
        trap |= signal_maskable(4);
    if (trap) {
        cpu_.raise_exception(Exception::FloatingPoint);
        return false;
    }
    return true;
}

bool Fpu::check_input_word(u32 bits) {
    if (is_subnormal_bits<float>(bits)) {
        signal_unimplemented();
        return false;
    }
    if (!is_nan_bits<float>(bits))
        return true;
    if (!nan_raises_invalid<float>(bits)) {
        signal_unimplemented();
        return false;
    }
    if (!signal_maskable(4))
        return true;
    cpu_.raise_exception(Exception::FloatingPoint);
    return false;
}

bool Fpu::check_input_doubleword(u64 bits) {
    if (is_subnormal_bits<double>(bits)) {
        signal_unimplemented();
        return false;
    }
    if (!is_nan_bits<double>(bits))
        return true;
    if (!nan_raises_invalid<double>(bits)) {
        signal_unimplemented();
        return false;
    }
    if (!signal_maskable(4))
        return true;
    cpu_.raise_exception(Exception::FloatingPoint);
    return false;
}

bool Fpu::check_inputs_word(u32 left, u32 right) {
    if ((is_nan_bits<float>(left) && !nan_raises_invalid<float>(left)) ||
        (is_nan_bits<float>(right) && !nan_raises_invalid<float>(right))) {
        signal_unimplemented();
        return false;
    }
    if (is_subnormal_bits<float>(left) || is_subnormal_bits<float>(right)) {
        signal_unimplemented();
        return false;
    }
    bool trap = false;
    if (is_nan_bits<float>(left) && nan_raises_invalid<float>(left))
        trap |= signal_maskable(4);
    if (is_nan_bits<float>(right) && nan_raises_invalid<float>(right))
        trap |= signal_maskable(4);
    if (!trap)
        return true;
    cpu_.raise_exception(Exception::FloatingPoint);
    return false;
}

bool Fpu::check_inputs_doubleword(u64 left, u64 right) {
    if ((is_nan_bits<double>(left) && !nan_raises_invalid<double>(left)) ||
        (is_nan_bits<double>(right) && !nan_raises_invalid<double>(right))) {
        signal_unimplemented();
        return false;
    }
    if (is_subnormal_bits<double>(left) || is_subnormal_bits<double>(right)) {
        signal_unimplemented();
        return false;
    }
    bool trap = false;
    if (is_nan_bits<double>(left) && nan_raises_invalid<double>(left))
        trap |= signal_maskable(4);
    if (is_nan_bits<double>(right) && nan_raises_invalid<double>(right))
        trap |= signal_maskable(4);
    if (!trap)
        return true;
    cpu_.raise_exception(Exception::FloatingPoint);
    return false;
}

bool Fpu::finish_word(u32& bits, bool host_underflow) {
    if (is_subnormal_bits<float>(bits) || (host_underflow && (bits & FloatBits<float>::exponent_mask) == 0)) {
        const bool flush = (control & (1U << 24)) != 0;
        if (!flush || (control & ((1U << 8) | (1U << 7))) != 0) {
            signal_unimplemented();
            return false;
        }
        signal_maskable(1);
        signal_maskable(0);
        const bool negative = (bits & FloatBits<float>::sign_mask) != 0;
        switch (control & 3U) {
        case 0:
        case 1:
            bits = negative ? FloatBits<float>::sign_mask : 0U;
            break;
        case 2:
            bits = negative ? FloatBits<float>::sign_mask : FloatBits<float>::minimum_normal;
            break;
        default:
            bits = negative ? (FloatBits<float>::sign_mask | FloatBits<float>::minimum_normal) : 0U;
            break;
        }
    } else if (is_nan_bits<float>(bits)) {
        bits = FloatBits<float>::canonical_nan;
    }
    return true;
}

bool Fpu::finish_doubleword(u64& bits, bool host_underflow) {
    if (is_subnormal_bits<double>(bits) ||
        (host_underflow && (bits & FloatBits<double>::exponent_mask) == 0)) {
        const bool flush = (control & (1U << 24)) != 0;
        if (!flush || (control & ((1U << 8) | (1U << 7))) != 0) {
            signal_unimplemented();
            return false;
        }
        signal_maskable(1);
        signal_maskable(0);
        const bool negative = (bits & FloatBits<double>::sign_mask) != 0;
        switch (control & 3U) {
        case 0:
        case 1:
            bits = negative ? FloatBits<double>::sign_mask : 0ULL;
            break;
        case 2:
            bits = negative ? FloatBits<double>::sign_mask : FloatBits<double>::minimum_normal;
            break;
        default:
            bits = negative ? (FloatBits<double>::sign_mask | FloatBits<double>::minimum_normal) : 0ULL;
            break;
        }
    } else if (is_nan_bits<double>(bits)) {
        bits = FloatBits<double>::canonical_nan;
    }
    return true;
}

void Fpu::execute(u32 instruction) {
    if (!cpu_.require_coprocessor(1))
        return;

    const unsigned operation = (instruction >> 21) & 31U;
    const unsigned rt = (instruction >> 16) & 31U;
    const unsigned fs = (instruction >> 11) & 31U;

    switch (operation) {
    case 0x00:
        cpu_.gpr[rt] = sign_extend32(read_word(fs));
        return;
    case 0x01:
        cpu_.gpr[rt] = read_doubleword(fs);
        return;
    case 0x02:
        cpu_.gpr[rt] = sign_extend32(read_control(fs));
        return;
    case 0x04:
        write_word(fs, static_cast<u32>(cpu_.gpr[rt]));
        return;
    case 0x05:
        write_doubleword(fs, cpu_.gpr[rt]);
        return;
    case 0x06:
        if (fs == 31U)
            set_control(static_cast<u32>(cpu_.gpr[rt]));
        return;
    case 0x08: {
        clear_causes();
        if (rt >= 4U) {
            signal_unimplemented();
            return;
        }
        const bool condition = ((control >> 23) & 1U) == (rt & 1U);
        const bool likely = (rt & 2U) != 0;
        const s64 displacement = static_cast<s64>(std::bit_cast<s16>(static_cast<u16>(instruction))) * 4;
        cpu_.branch(condition, cpu_.next_pc + static_cast<u64>(displacement), likely);
        return;
    }
    case 0x10:
    case 0x11:
    case 0x14:
    case 0x15:
        execute_format(instruction, operation);
        return;
    default:
        clear_causes();
        signal_unimplemented();
        return;
    }
}

void Fpu::execute_compare(u32 instruction, unsigned format) {
    const unsigned ft = (instruction >> 16) & 31U;
    const unsigned fs = (instruction >> 11) & 31U;
    const unsigned function = instruction & 63U;
    const bool signaling = (function & 8U) != 0;
    const bool unordered_result = (function & 1U) != 0;

    bool less = false;
    bool equal = false;
    bool unordered = false;
    bool trap = false;

    if (format == 0x10U) {
        const u32 left_bits = source_word(fs);
        const u32 right_bits = second_source_word(ft);
        const bool left_nan = is_nan_bits<float>(left_bits);
        const bool right_nan = is_nan_bits<float>(right_bits);
        unordered = left_nan || right_nan;
        if (left_nan && (signaling || nan_raises_invalid<float>(left_bits)))
            trap |= signal_maskable(4);
        if (right_nan && (signaling || nan_raises_invalid<float>(right_bits)))
            trap |= signal_maskable(4);
        if (!unordered) {
            const float left = value_of<float>(left_bits);
            const float right = value_of<float>(right_bits);
            less = left < right;
            equal = left == right;
        }
    } else {
        const u64 left_bits = source_doubleword(fs);
        const u64 right_bits = second_source_doubleword(ft);
        const bool left_nan = is_nan_bits<double>(left_bits);
        const bool right_nan = is_nan_bits<double>(right_bits);
        unordered = left_nan || right_nan;
        if (left_nan && (signaling || nan_raises_invalid<double>(left_bits)))
            trap |= signal_maskable(4);
        if (right_nan && (signaling || nan_raises_invalid<double>(right_bits)))
            trap |= signal_maskable(4);
        if (!unordered) {
            const double left = value_of<double>(left_bits);
            const double right = value_of<double>(right_bits);
            less = left < right;
            equal = left == right;
        }
    }

    if (trap) {
        cpu_.raise_exception(Exception::FloatingPoint);
        return;
    }

    const bool result =
        unordered ? unordered_result : (((function & 4U) != 0 && less) || ((function & 2U) != 0 && equal));
    control = (control & ~(1U << 23)) | (static_cast<u32>(result) << 23);
}

void Fpu::execute_format(u32 instruction, unsigned format) {
    const unsigned ft = (instruction >> 16) & 31U;
    const unsigned fs = (instruction >> 11) & 31U;
    const unsigned fd = (instruction >> 6) & 31U;
    const unsigned function = instruction & 63U;

    if ((format == 0x10U || format == 0x11U) && function == 0x06U) {
        write_result_doubleword(fd, source_doubleword(fs));
        return;
    }

    ScopedEnvironment environment(control & 3U);
    clear_causes();

    if ((format == 0x10U || format == 0x11U) && function >= 0x30U) {
        execute_compare(instruction, format);
        return;
    }

    auto raise_unimplemented = [this] { signal_unimplemented(); };

    auto convert_single_to_integer = [this, fs, fd](bool long_result, u32 rounding_mode) {
        const u32 input_bits = source_word(fs);
        if (is_subnormal_bits<float>(input_bits) || is_infinite_bits<float>(input_bits) ||
            is_nan_bits<float>(input_bits)) {
            cpu_.add_cycles(1);
            signal_unimplemented();
            return;
        }
        const float value = value_of<float>(input_bits);
        const float early_limit = long_result ? 0x1p53f : 0x1p32f;
        cpu_.add_cycles(value >= early_limit || value <= -early_limit ? 1 : 4);
        if (long_result) {
            if (value >= 0x1p+53f || value <= -0x1p+53f) {
                signal_unimplemented();
                return;
            }
        } else if (value >= 0x1p+31f || value < -0x1p+31f) {
            signal_unimplemented();
            return;
        }

        const float rounded = round_integral(value, rounding_mode);
        if (!long_result && (rounded > static_cast<float>(std::numeric_limits<s32>::max()) ||
                             rounded < static_cast<float>(std::numeric_limits<s32>::min()))) {
            signal_unimplemented();
            return;
        }
        if (rounded != value && signal_maskable(0)) {
            cpu_.raise_exception(Exception::FloatingPoint);
            return;
        }
        if (long_result) {
            write_result_doubleword(fd, std::bit_cast<u64>(static_cast<s64>(rounded)));
        } else {
            write_result_word(fd, std::bit_cast<u32>(static_cast<s32>(rounded)));
        }
    };

    auto convert_double_to_integer = [this, fs, fd](bool long_result, u32 rounding_mode) {
        const u64 input_bits = source_doubleword(fs);
        if (is_subnormal_bits<double>(input_bits) || is_infinite_bits<double>(input_bits) ||
            is_nan_bits<double>(input_bits)) {
            cpu_.add_cycles(1);
            signal_unimplemented();
            return;
        }
        const double value = value_of<double>(input_bits);
        const double early_limit = long_result ? 0x1p53 : 0x1p32;
        cpu_.add_cycles(value >= early_limit || value <= -early_limit ? 1 : 4);
        if (long_result) {
            if (value >= 0x1p+53 || value <= -0x1p+53) {
                signal_unimplemented();
                return;
            }
        } else if (value >= 0x1p+31 || value < -0x1p+31) {
            signal_unimplemented();
            return;
        }

        const double rounded = round_integral(value, rounding_mode);
        if (!long_result && (rounded > static_cast<double>(std::numeric_limits<s32>::max()) ||
                             rounded < static_cast<double>(std::numeric_limits<s32>::min()))) {
            signal_unimplemented();
            return;
        }
        if (rounded != value && signal_maskable(0)) {
            cpu_.raise_exception(Exception::FloatingPoint);
            return;
        }
        if (long_result) {
            write_result_doubleword(fd, std::bit_cast<u64>(static_cast<s64>(rounded)));
        } else {
            write_result_word(fd, std::bit_cast<u32>(static_cast<s32>(rounded)));
        }
    };

    if (format == 0x14U || format == 0x15U) {
        const bool zero_integer = format == 0x14U ? source_word(fs) == 0 : source_doubleword(fs) == 0;
        if (function != 0x20U && function != 0x21U) {
            if (function < 0x10U || function == 0x24U || function == 0x25U || function >= 0x30U)
                cpu_.add_cycles(1);
            raise_unimplemented();
            return;
        }

        std::feclearexcept(FE_ALL_EXCEPT);
        if (format == 0x14U) {
            cpu_.add_cycles(zero_integer ? 1 : 4);
            const volatile s32 input = std::bit_cast<s32>(source_word(fs));
            if (function == 0x20U) {
                const volatile float converted = static_cast<float>(input);
                const int exceptions = std::fetestexcept(FE_ALL_EXCEPT);
                if (!handle_host_exceptions(exceptions, false))
                    return;
                u32 result = bits_of<float>(converted);
                if (!finish_word(result, (exceptions & FE_UNDERFLOW) != 0))
                    return;
                write_result_word(fd, result);
            } else {
                const volatile double converted = static_cast<double>(input);
                const int exceptions = std::fetestexcept(FE_ALL_EXCEPT);
                if (!handle_host_exceptions(exceptions, false))
                    return;
                u64 result = bits_of<double>(converted);
                if (!finish_doubleword(result, (exceptions & FE_UNDERFLOW) != 0))
                    return;
                write_result_doubleword(fd, result);
            }
        } else {
            const s64 integer = std::bit_cast<s64>(source_doubleword(fs));
            if (integer >= static_cast<s64>(0x0080000000000000ULL) ||
                integer < -static_cast<s64>(0x0080000000000000ULL)) {
                cpu_.add_cycles(1);
                signal_unimplemented();
                return;
            }
            cpu_.add_cycles(zero_integer ? 1 : 4);
            const volatile s64 input = integer;
            if (function == 0x20U) {
                const volatile float converted = static_cast<float>(input);
                const int exceptions = std::fetestexcept(FE_ALL_EXCEPT);
                if (!handle_host_exceptions(exceptions, false))
                    return;
                u32 result = bits_of<float>(converted);
                if (!finish_word(result, (exceptions & FE_UNDERFLOW) != 0))
                    return;
                write_result_word(fd, result);
            } else {
                const volatile double converted = static_cast<double>(input);
                const int exceptions = std::fetestexcept(FE_ALL_EXCEPT);
                if (!handle_host_exceptions(exceptions, false))
                    return;
                u64 result = bits_of<double>(converted);
                if (!finish_doubleword(result, (exceptions & FE_UNDERFLOW) != 0))
                    return;
                write_result_doubleword(fd, result);
            }
        }
        return;
    }

    if (format != 0x10U && format != 0x11U) {
        raise_unimplemented();
        return;
    }

    const bool single = format == 0x10U;

    if (function <= 0x03U) {
        std::feclearexcept(FE_ALL_EXCEPT);
        if (single) {
            const u32 left_bits = source_word(fs);
            const u32 right_bits = second_source_word(ft);
            if (!check_inputs_word(left_bits, right_bits)) {
                cpu_.add_cycles(1);
                return;
            }
            const volatile float left = value_of<float>(left_bits);
            const volatile float right = value_of<float>(right_bits);
            volatile float output = 0.0f;
            switch (function) {
            case 0:
                output = left + right;
                break;
            case 1:
                output = left - right;
                break;
            case 2:
                output = left * right;
                break;
            default:
                output = left / right;
                break;
            }
            const int exceptions = std::fetestexcept(FE_ALL_EXCEPT);
            const bool flush_underflow =
                (exceptions & FE_UNDERFLOW) != 0 && (control & (1U << 24)) != 0 && (control & 0x180U) == 0;
            cpu_.add_cycles(arithmetic_latency<float>(function, left_bits, right_bits, flush_underflow));
            if (!handle_host_exceptions(exceptions, false))
                return;
            u32 result = bits_of<float>(output);
            if (!finish_word(result, (exceptions & FE_UNDERFLOW) != 0))
                return;
            write_result_word(fd, result);
        } else {
            const u64 left_bits = source_doubleword(fs);
            const u64 right_bits = second_source_doubleword(ft);
            if (!check_inputs_doubleword(left_bits, right_bits)) {
                cpu_.add_cycles(1);
                return;
            }
            const volatile double left = value_of<double>(left_bits);
            const volatile double right = value_of<double>(right_bits);
            volatile double output = 0.0;
            switch (function) {
            case 0:
                output = left + right;
                break;
            case 1:
                output = left - right;
                break;
            case 2:
                output = left * right;
                break;
            default:
                output = left / right;
                break;
            }
            const int exceptions = std::fetestexcept(FE_ALL_EXCEPT);
            const bool flush_underflow =
                (exceptions & FE_UNDERFLOW) != 0 && (control & (1U << 24)) != 0 && (control & 0x180U) == 0;
            cpu_.add_cycles(arithmetic_latency<double>(function, left_bits, right_bits, flush_underflow));
            if (!handle_host_exceptions(exceptions, false))
                return;
            u64 result = bits_of<double>(output);
            if (!finish_doubleword(result, (exceptions & FE_UNDERFLOW) != 0))
                return;
            write_result_doubleword(fd, result);
        }
        return;
    }

    if (function == 0x04U) {
        std::feclearexcept(FE_ALL_EXCEPT);
        if (single) {
            const u32 input_bits = source_word(fs);
            if (!check_input_word(input_bits)) {
                cpu_.add_cycles(1);
                return;
            }
            cpu_.add_cycles(arithmetic_shortcut<float>(input_bits) ||
                                    (input_bits & FloatBits<float>::sign_mask) != 0
                                ? 1
                                : 28);
            const volatile float input = value_of<float>(input_bits);
            const volatile float output = std::sqrt(input);
            const int exceptions = std::fetestexcept(FE_ALL_EXCEPT);
            if (!handle_host_exceptions(exceptions, false))
                return;
            u32 result = bits_of<float>(output);
            if (!finish_word(result, (exceptions & FE_UNDERFLOW) != 0))
                return;
            write_result_word(fd, result);
        } else {
            const u64 input_bits = source_doubleword(fs);
            if (!check_input_doubleword(input_bits)) {
                cpu_.add_cycles(1);
                return;
            }
            cpu_.add_cycles(arithmetic_shortcut<double>(input_bits) ||
                                    (input_bits & FloatBits<double>::sign_mask) != 0
                                ? 1
                                : 57);
            const volatile double input = value_of<double>(input_bits);
            const volatile double output = std::sqrt(input);
            const int exceptions = std::fetestexcept(FE_ALL_EXCEPT);
            if (!handle_host_exceptions(exceptions, false))
                return;
            u64 result = bits_of<double>(output);
            if (!finish_doubleword(result, (exceptions & FE_UNDERFLOW) != 0))
                return;
            write_result_doubleword(fd, result);
        }
        return;
    }

    if (function == 0x05U || function == 0x07U) {
        if (single) {
            u32 result = source_word(fs);
            if (!check_input_word(result))
                return;
            if (function == 0x05U)
                result &= ~FloatBits<float>::sign_mask;
            else
                result ^= FloatBits<float>::sign_mask;
            if (!finish_word(result))
                return;
            write_result_word(fd, result);
        } else {
            u64 result = source_doubleword(fs);
            if (!check_input_doubleword(result))
                return;
            if (function == 0x05U)
                result &= ~FloatBits<double>::sign_mask;
            else
                result ^= FloatBits<double>::sign_mask;
            if (!finish_doubleword(result))
                return;
            write_result_doubleword(fd, result);
        }
        return;
    }

    if (function >= 0x08U && function <= 0x0fU) {
        const bool long_result = function < 0x0cU;
        u32 rounding_mode = 0;
        switch (function & 3U) {
        case 0:
            rounding_mode = 0;
            break;
        case 1:
            rounding_mode = 1;
            break;
        case 2:
            rounding_mode = 2;
            break;
        default:
            rounding_mode = 3;
            break;
        }
        if (single)
            convert_single_to_integer(long_result, rounding_mode);
        else
            convert_double_to_integer(long_result, rounding_mode);
        return;
    }

    if (function == 0x20U) {
        if (single) {
            raise_unimplemented();
            return;
        }
        cpu_.add_cycles(1);
        const u64 input_bits = source_doubleword(fs);
        if (!check_input_doubleword(input_bits))
            return;
        std::feclearexcept(FE_ALL_EXCEPT);
        const volatile double input = value_of<double>(input_bits);
        const volatile float output = static_cast<float>(input);
        const int exceptions = std::fetestexcept(FE_ALL_EXCEPT);
        if (!handle_host_exceptions(exceptions, false))
            return;
        u32 result = bits_of<float>(output);
        if (!finish_word(result, (exceptions & FE_UNDERFLOW) != 0))
            return;
        write_result_word(fd, result);
        return;
    }

    if (function == 0x21U) {
        if (!single) {
            raise_unimplemented();
            return;
        }
        const u32 input_bits = source_word(fs);
        if (!check_input_word(input_bits))
            return;
        std::feclearexcept(FE_ALL_EXCEPT);
        const volatile float input = value_of<float>(input_bits);
        const volatile double output = static_cast<double>(input);
        const int exceptions = std::fetestexcept(FE_ALL_EXCEPT);
        if (!handle_host_exceptions(exceptions, false))
            return;
        u64 result = bits_of<double>(output);
        if (!finish_doubleword(result, (exceptions & FE_UNDERFLOW) != 0))
            return;
        write_result_doubleword(fd, result);
        return;
    }

    if (function == 0x24U || function == 0x25U) {
        const bool long_result = function == 0x25U;
        if (single)
            convert_single_to_integer(long_result, control & 3U);
        else
            convert_double_to_integer(long_result, control & 3U);
        return;
    }

    raise_unimplemented();
}

} // namespace cupid
