#include "core/rsp/vector/execute.hpp"
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <smmintrin.h>
#include <utility>

namespace cupid::n64 {
namespace {

__m128i load(const RspVector &vector) {
  return _mm_load_si128(reinterpret_cast<const __m128i *>(&vector));
}

void store(RspVector &vector, __m128i value) {
  _mm_store_si128(reinterpret_cast<__m128i *>(&vector), value);
}

struct Tables {
  alignas(16) std::array<std::array<std::uint8_t, 16>, 16> select{};
  std::array<RspVector, 256> flags{};
  constexpr Tables() {
    for (unsigned element = 0; element < 16; ++element)
      for (unsigned lane = 0; lane < 8; ++lane) {
        const auto index = element < 2   ? lane
                           : element < 4 ? (lane & 6) | (element & 1)
                           : element < 8 ? (lane & 4) | (element & 3)
                                         : element & 7;
        select[element][lane * 2] = static_cast<std::uint8_t>(index * 2);
        select[element][lane * 2 + 1] = static_cast<std::uint8_t>(index * 2 + 1);
      }
    for (unsigned mask = 0; mask < 256; ++mask)
      for (unsigned lane = 0; lane < 8; ++lane)
        flags[mask].lanes[lane] = (mask >> lane) & 1 ? 0xffff : 0;
  }
};
constexpr Tables tables;

__m128i flags(std::uint8_t mask) {
  return load(tables.flags[mask]);
}

std::uint8_t flags(__m128i value) {
  return static_cast<std::uint8_t>(_mm_movemask_epi8(_mm_packs_epi16(value, _mm_setzero_si128())));
}

__m128i less_unsigned(__m128i a, __m128i b) {
  const auto sign = _mm_set1_epi16(-32768);
  return _mm_cmpgt_epi16(_mm_xor_si128(b, sign), _mm_xor_si128(a, sign));
}

struct Accumulator {
  __m128i low, middle, high;
  void add(const Accumulator &value) {
    const auto lo = _mm_add_epi16(low, value.low);
    const auto carry_low = less_unsigned(lo, low);
    const auto mid = _mm_add_epi16(middle, value.middle);
    auto carry_middle = less_unsigned(mid, middle);
    const auto with_carry = _mm_sub_epi16(mid, carry_low);
    carry_middle = _mm_or_si128(carry_middle, less_unsigned(with_carry, mid));
    high = _mm_sub_epi16(_mm_add_epi16(high, value.high), carry_middle);
    middle = with_carry;
    low = lo;
  }
  __m128i signed_middle() const {
    return _mm_packs_epi32(_mm_unpacklo_epi16(middle, high), _mm_unpackhi_epi16(middle, high));
  }
  __m128i unsigned_middle() const {
    const auto negative = _mm_srai_epi16(high, 15);
    const auto overflow =
        _mm_or_si128(_mm_srai_epi16(middle, 15),
                     _mm_xor_si128(_mm_cmpeq_epi16(high, _mm_setzero_si128()), _mm_set1_epi16(-1)));
    return _mm_andnot_si128(negative, _mm_or_si128(middle, overflow));
  }
  __m128i unsigned_low() const {
    const auto inside = _mm_cmpeq_epi16(high, _mm_srai_epi16(middle, 15));
    const auto overflow = _mm_xor_si128(_mm_srai_epi16(high, 15), _mm_set1_epi16(-1));
    return _mm_blendv_epi8(overflow, low, inside);
  }
  void save(RspAccumulator &dest) const {
    store(dest.low, low);
    store(dest.middle, middle);
    store(dest.high, high);
  }
};

Accumulator multiply(__m128i a, __m128i b, unsigned operation) {
  const auto zero = _mm_setzero_si128();
  const auto lo = _mm_mullo_epi16(a, b);
  auto mid = _mm_mulhi_epi16(a, b);
  switch (operation & 7) {
  case 0:
  case 1:
    return {_mm_slli_epi16(lo, 1), _mm_add_epi16(_mm_slli_epi16(mid, 1), _mm_srli_epi16(lo, 15)),
            _mm_srai_epi16(mid, 15)};
  case 4:
    return {_mm_mulhi_epu16(a, b), zero, zero};
  case 5:
    mid = _mm_sub_epi16(_mm_mulhi_epu16(a, b), _mm_and_si128(_mm_srai_epi16(a, 15), b));
    return {lo, mid, _mm_srai_epi16(mid, 15)};
  case 6:
    mid = _mm_sub_epi16(_mm_mulhi_epu16(a, b), _mm_and_si128(_mm_srai_epi16(b, 15), a));
    return {lo, mid, _mm_srai_epi16(mid, 15)};
  default:
    return {zero, lo, mid};
  }
}

__m128i add_signed(__m128i a, __m128i b, __m128i carry, bool subtract) {
  const auto a_sign = _mm_srai_epi16(a, 15);
  const auto b_sign = _mm_srai_epi16(b, 15);
  auto lo = subtract ? _mm_sub_epi32(_mm_unpacklo_epi16(a, a_sign), _mm_unpacklo_epi16(b, b_sign))
                     : _mm_add_epi32(_mm_unpacklo_epi16(a, a_sign), _mm_unpacklo_epi16(b, b_sign));
  auto hi = subtract ? _mm_sub_epi32(_mm_unpackhi_epi16(a, a_sign), _mm_unpackhi_epi16(b, b_sign))
                     : _mm_add_epi32(_mm_unpackhi_epi16(a, a_sign), _mm_unpackhi_epi16(b, b_sign));
  const auto zero = _mm_setzero_si128();
  carry = _mm_and_si128(carry, _mm_set1_epi16(1));
  lo = subtract ? _mm_sub_epi32(lo, _mm_unpacklo_epi16(carry, zero))
                : _mm_add_epi32(lo, _mm_unpacklo_epi16(carry, zero));
  hi = subtract ? _mm_sub_epi32(hi, _mm_unpackhi_epi16(carry, zero))
                : _mm_add_epi32(hi, _mm_unpackhi_epi16(carry, zero));
  return _mm_packs_epi32(lo, hi);
}

} // namespace

constexpr bool supported(unsigned operation) {
  switch (operation) {
  case 0:
  case 1:
  case 4:
  case 5:
  case 6:
  case 7:
  case 8:
  case 9:
  case 12:
  case 13:
  case 14:
  case 15:
  case 16:
  case 17:
  case 19:
  case 20:
  case 21:
  case 29:
  case 32:
  case 33:
  case 34:
  case 35:
  case 39:
  case 40:
  case 41:
  case 42:
  case 43:
  case 44:
  case 45:
    return true;
  case 55:
  case 63:
    return true;
  default:
    return false;
  }
}

template <int Operation = -1, int Element = -1>
void execute(RspState &state, RspVector &dest, const RspVector &source, const RspVector &target,
             unsigned dynamic_operation = 0, unsigned dynamic_element = 0) {
  const auto operation = Operation < 0 ? dynamic_operation : unsigned(Operation);
  const auto element = Element < 0 ? dynamic_element : unsigned(Element);
  if (operation == 55 || operation == 63)
    return;
  const auto a = load(source);
  const auto b = [&] {
    if constexpr (Element == 0 || Element == 1)
      return load(target);
    else {
      const auto selection =
          _mm_load_si128(reinterpret_cast<const __m128i *>(&tables.select[element]));
      return _mm_shuffle_epi8(load(target), selection);
    }
  }();
  const auto zero = _mm_setzero_si128();
  const auto invert = _mm_set1_epi16(-1);
  __m128i result;
  bool write_low = false;
  if (operation <= 15) {
    auto acc = multiply(a, b, operation);
    if (operation == 0 || operation == 1)
      acc.add({_mm_set1_epi16(-32768), zero, zero});
    else if (operation >= 8) {
      auto current = Accumulator{load(state.accumulator.low), load(state.accumulator.middle),
                                 load(state.accumulator.high)};
      current.add(acc);
      acc = current;
    }
    if (operation == 1 || operation == 9)
      result = acc.unsigned_middle();
    else if (operation == 4 || operation == 6)
      result = acc.low;
    else if (operation == 5)
      result = acc.middle;
    else if (operation == 12 || operation == 14)
      result = acc.unsigned_low();
    else
      result = acc.signed_middle();
    acc.save(state.accumulator);
  } else {
    switch (operation) {
    case 16:
    case 17: {
      const auto carry = flags(state.carry_low);
      const auto low = operation == 16 ? _mm_sub_epi16(_mm_add_epi16(a, b), carry)
                                       : _mm_add_epi16(_mm_sub_epi16(a, b), carry);
      store(state.accumulator.low, low);
      result = add_signed(a, b, carry, operation == 17);
      state.carry_low = state.carry_high = 0;
      break;
    }
    case 19: {
      const auto negative = _mm_cmpgt_epi16(zero, a);
      const auto positive = _mm_cmpgt_epi16(a, zero);
      const auto value =
          _mm_or_si128(_mm_and_si128(_mm_sub_epi16(zero, b), negative), _mm_and_si128(b, positive));
      store(state.accumulator.low, value);
      result = _mm_or_si128(_mm_and_si128(_mm_subs_epi16(zero, b), negative),
                            _mm_and_si128(b, positive));
      break;
    }
    case 20:
      result = _mm_add_epi16(a, b);
      state.carry_low = flags(less_unsigned(result, a));
      state.carry_high = 0;
      write_low = true;
      break;
    case 21:
      result = _mm_sub_epi16(a, b);
      state.carry_low = flags(less_unsigned(a, b));
      state.carry_high = flags(_mm_xor_si128(_mm_cmpeq_epi16(result, zero), invert));
      write_low = true;
      break;
    case 29:
      result = element == 8    ? load(state.accumulator.high)
               : element == 9  ? load(state.accumulator.middle)
               : element == 10 ? load(state.accumulator.low)
                               : zero;
      break;
    case 32:
    case 33:
    case 34:
    case 35: {
      const auto equal = _mm_cmpeq_epi16(a, b);
      const auto carry_high = flags(state.carry_high);
      const auto both = _mm_and_si128(flags(state.carry_low), carry_high);
      __m128i condition;
      if (operation == 32)
        condition = _mm_or_si128(_mm_cmpgt_epi16(b, a), _mm_and_si128(equal, both));
      else if (operation == 33)
        condition = _mm_andnot_si128(carry_high, equal);
      else if (operation == 34)
        condition = _mm_or_si128(_mm_xor_si128(equal, invert), carry_high);
      else
        condition = _mm_or_si128(_mm_cmpgt_epi16(a, b), _mm_andnot_si128(both, equal));
      result = _mm_blendv_epi8(b, a, condition);
      state.compare_low = flags(condition);
      state.compare_high = 0;
      state.carry_low = state.carry_high = 0;
      write_low = true;
      break;
    }
    case 39:
      result = _mm_blendv_epi8(b, a, flags(state.compare_low));
      state.carry_low = state.carry_high = 0;
      write_low = true;
      break;
    default:
      result = operation <= 41   ? _mm_and_si128(a, b)
               : operation <= 43 ? _mm_or_si128(a, b)
                                 : _mm_xor_si128(a, b);
      if (operation & 1)
        result = _mm_xor_si128(result, invert);
      write_low = true;
      break;
    }
  }
  if (write_low)
    store(state.accumulator.low, result);
  store(dest, result);
}

template <unsigned Operation, unsigned Element>
void handler(RspState *state, RspVector *dest, const RspVector *source, const RspVector *target) {
  execute<Operation, Element>(*state, *dest, *source, *target);
}

template <unsigned Operation, unsigned Element> constexpr VectorHandler select_handler() {
  if constexpr (supported(Operation))
    return &handler<Operation, Element>;
  return nullptr;
}

template <std::size_t... Index> constexpr auto handlers(std::index_sequence<Index...>) {
  return std::array<VectorHandler, sizeof...(Index)>{select_handler<Index / 16, Index % 16>()...};
}

VectorHandler vector_sse_handler(unsigned operation, unsigned element) {
  static constexpr auto table = handlers(std::make_index_sequence<1024>{});
  return table[operation * 16 + element];
}

bool execute_vector_sse(RspState &state, std::uint32_t instruction) {
  if (!supported(instruction & 63))
    return false;
  execute(state, state.vectors[(instruction >> 6) & 31], state.vectors[(instruction >> 11) & 31],
          state.vectors[(instruction >> 16) & 31], instruction & 63, (instruction >> 21) & 15);
  return true;
}

} // namespace cupid::n64
#else
namespace cupid::n64 {
VectorHandler vector_sse_handler(unsigned, unsigned) {
  return nullptr;
}

bool execute_vector_sse(RspState &, std::uint32_t) {
  return false;
}
} // namespace cupid::n64
#endif
