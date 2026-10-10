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

} // namespace

int main(int argc, char **argv) {
  if (argc > 1)
    return core_state_file_test(argc, argv);
  archive_tests();
  core_state_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
