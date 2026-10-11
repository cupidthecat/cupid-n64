#include "core/state/archive.hpp"
#include "../support/test.hpp"

using cupid::n64::state::Archive;
using cupid::n64::state::InvalidState;

void core_state_tests();
int core_state_file_test(int argc, char **argv);

namespace {

void checksum(std::vector<std::uint8_t> &bytes) {
  const auto value = Archive::fingerprint(std::span(bytes).first(bytes.size() - 8));
  for (unsigned n = 0; n < 8; ++n)
    bytes[bytes.size() - 8 + n] = static_cast<std::uint8_t>(value >> (n * 8));
}

template <typename Function> bool rejects(Function function) {
  try {
    function();
  } catch (const InvalidState &) {
    return true;
  }
  return false;
}

void archive_tests() {
  std::uint32_t word = 0x12345678;
  std::int64_t negative = -2;
  bool enabled = true;
  double number = -0.0;
  std::array<std::uint16_t, 2> array{0x1234, 0xabcd};
  Archive writer;
  writer.fields(word, negative, enabled, number);
  writer.array(array);
  const auto bytes = writer.finish();
  const std::array<std::uint8_t, 25> golden{0x78, 0x56, 0x34, 0x12, 0xfe, 0xff, 0xff, 0xff, 0xff,
                                            0xff, 0xff, 0xff, 1,    0,    0,    0,    0,    0,
                                            0,    0,    0x80, 0x34, 0x12, 0xcd, 0xab};
  test::equal(bytes.size(), golden.size() + 8);
  test::equal(std::equal(golden.begin(), golden.end(), bytes.begin()), true);
  const std::array<std::uint8_t, 5> hello{'h', 'e', 'l', 'l', 'o'};
  test::equal(Archive::fingerprint(hello), 0xa430d84680aabd0bull);

  word = 9;
  negative = 7;
  enabled = false;
  number = 1;
  array = {};
  {
    Archive reader(bytes);
    reader.fields(word, negative, enabled, number);
    reader.array(array);
    test::equal(word, 9);
    test::equal(negative, 7);
    test::equal(enabled, false);
    reader.finish();
  }
  test::equal(word, 0x12345678);
  test::equal(std::bit_cast<std::uint64_t>(negative), 0xfffffffffffffffeull);
  test::equal(enabled, true);
  test::equal(std::bit_cast<std::uint64_t>(number), 0x8000000000000000ull);
  test::equal(array[0], 0x1234);
  test::equal(array[1], 0xabcd);

  for (std::size_t length = 0; length < bytes.size(); ++length) {
    word = 91;
    test::equal(rejects([&] {
                  Archive reader{std::span(bytes).first(length)};
                  reader.fields(word, negative, enabled, number);
                  reader.array(array);
                  reader.finish();
                }),
                true);
    test::equal(word, 91);
  }
  for (unsigned fault = 0; fault < 4; ++fault) {
    auto damaged = bytes;
    if (fault == 0)
      damaged[0] ^= 1;
    if (fault == 1) {
      damaged[12] = 2;
      checksum(damaged);
    }
    if (fault == 2) {
      damaged.insert(damaged.end() - 8, 0);
      checksum(damaged);
    }
    if (fault == 3) {
      damaged.erase(damaged.begin() + 22);
      checksum(damaged);
    }
    word = 91;
    enabled = false;
    array = {};
    test::equal(rejects([&] {
                  Archive reader(damaged);
                  reader.fields(word, negative, enabled, number);
                  reader.array(array);
                  reader.finish();
                }),
                true);
    test::equal(word, 91);
    test::equal(enabled, false);
    test::equal(array[1], 0);
  }

  Archive vector_writer;
  std::vector<std::uint8_t> values{1, 2, 3};
  vector_writer.vector(values, 4);
  auto vector_bytes = vector_writer.finish();
  for (unsigned fault = 0; fault < 2; ++fault) {
    auto damaged = vector_bytes;
    if (fault == 0)
      damaged[0] = 5;
    else
      damaged[0] = 4;
    checksum(damaged);
    values = {8, 9};
    test::equal(rejects([&] {
                  Archive reader(damaged);
                  reader.vector(values, 4);
                  reader.finish();
                }),
                true);
    test::equal(values.size(), 2);
    test::equal(values[0], 8);
  }
  {
    Archive reader(vector_bytes);
    reader.vector(values, 4);
  }
  test::equal(values.size(), 2);
  test::equal(values[0], 8);
  Archive reader(vector_bytes);
  reader.vector(values, 4);
  reader.finish();
  test::equal(values.size(), 3);
  test::equal(values[2], 3);
}

enum class ArrayValue : std::int16_t { Negative = -2, Positive = 0x1234 };

template <typename T, std::size_t Size>
void array_roundtrip(std::array<T, Size> values, std::span<const std::uint8_t> golden) {
  Archive writer;
  writer.array(values);
  const auto encoded = writer.finish();
  test::equal(encoded.size(), golden.size() + 8);
  test::equal(std::equal(golden.begin(), golden.end(), encoded.begin()), true);
  std::array<T, Size> target{};
  Archive reader(encoded);
  reader.array(target);
  test::equal(target == std::array<T, Size>{}, true);
  reader.finish();
  for (unsigned n = 0; n < Size; ++n) {
    if constexpr (std::is_same_v<T, double>)
      test::equal(std::bit_cast<std::uint64_t>(target[n]), std::bit_cast<std::uint64_t>(values[n]));
    else if constexpr (std::is_enum_v<T>)
      test::equal(static_cast<std::underlying_type_t<T>>(target[n]),
                  static_cast<std::underlying_type_t<T>>(values[n]));
    else
      test::equal(target[n], values[n]);
  }
}

void array_tests() {
  const std::array<std::uint8_t, 4> bytes{0, 0x7f, 0x80, 0xff};
  array_roundtrip(bytes, bytes);
  const std::array<std::uint8_t, 8> signed_golden{0xfe, 0xff, 0xff, 0xff, 0x78, 0x56, 0x34, 0x12};
  array_roundtrip(std::array<std::int32_t, 2>{-2, 0x12345678}, signed_golden);
  const std::array<std::uint8_t, 4> enum_golden{0xfe, 0xff, 0x34, 0x12};
  array_roundtrip(std::array<ArrayValue, 2>{ArrayValue::Negative, ArrayValue::Positive},
                  enum_golden);
  const std::array<std::uint8_t, 4> bool_golden{0, 1, 1, 0};
  array_roundtrip(std::array<bool, 4>{false, true, true, false}, bool_golden);
  const std::array<double, 3> floating{-0.0, std::bit_cast<double>(0x7ff800000000cafeull),
                                       std::bit_cast<double>(0xfff0000000000123ull)};
  const std::array<std::uint8_t, 24> floating_golden{0,    0,    0, 0, 0, 0, 0,    0x80,
                                                     0xfe, 0xca, 0, 0, 0, 0, 0xf8, 0x7f,
                                                     0x23, 1,    0, 0, 0, 0, 0xf0, 0xff};
  array_roundtrip(floating, floating_golden);
  array_roundtrip(std::array<std::uint64_t, 0>{}, std::span<const std::uint8_t>{});

  const std::vector<std::uint32_t> values{0x12345678, 0xa5f0b3c2};
  Archive writer;
  writer.owned_vector(values, 2);
  const auto encoded = writer.finish();
  Archive reader(encoded);
  const auto decoded = reader.owned_vector(std::vector<std::uint32_t>{}, 2);
  reader.finish();
  test::equal(decoded == values, true);
  for (unsigned fault = 0; fault < 3; ++fault) {
    auto damaged = encoded;
    if (fault == 0)
      damaged[0] = 3;
    if (fault == 1)
      damaged.erase(damaged.end() - 9);
    if (fault == 2)
      damaged.insert(damaged.end() - 8, 0);
    checksum(damaged);
    std::vector<std::uint32_t> target{0xdeadbeef};
    test::equal(rejects([&] {
                  Archive malformed(damaged);
                  malformed.vector(target, 2);
                  malformed.finish();
                }),
                true);
    test::equal(target == std::vector<std::uint32_t>{0xdeadbeef}, true);
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1)
    return core_state_file_test(argc, argv);
  archive_tests();
  array_tests();
  core_state_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
