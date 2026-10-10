#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace cupid::n64::state {

static_assert(sizeof(unsigned) == 4 && sizeof(int) == 4);

class InvalidState : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct StateRange {
  std::string name;
  std::size_t offset = 0, bytes = 0;
  unsigned element_bytes = 1;
};

class Archive {
public:
  Archive() = default;
  explicit Archive(bool describe) : describe_(describe) {}
  void label(std::string_view name, unsigned element_bytes = 1) {
    if (!describe_)
      return;
    close_range();
    ranges_.push_back({std::string(name), output_.size(), 0, element_bytes});
  }
  void member_label(std::string_view name, std::string_view member, unsigned element_bytes = 1) {
    if (describe_)
      label(std::string(name) + "." + std::string(member), element_bytes);
  }
  void indexed_label(std::string_view name, std::size_t index, std::string_view member,
                     unsigned element_bytes = 1) {
    if (describe_)
      label(std::string(name) + "[" + std::to_string(index) + "]." + std::string(member),
            element_bytes);
  }
  std::vector<StateRange> ranges() {
    close_range();
    return std::move(ranges_);
  }
  explicit Archive(std::span<const std::uint8_t> input);
  bool loading() const {
    return loading_;
  }
  static std::uint64_t fingerprint(std::span<const std::uint8_t> bytes);
  static void require(bool condition);

  template <typename T> T literal(T value) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T> || std::is_same_v<T, double>);
    if constexpr (std::is_enum_v<T>) {
      return static_cast<T>(literal(static_cast<std::underlying_type_t<T>>(value)));
    } else if constexpr (std::is_same_v<T, bool>) {
      const auto bits = literal<std::uint8_t>(value ? 1 : 0);
      require(bits <= 1);
      return bits != 0;
    } else if constexpr (std::is_same_v<T, double>) {
      static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
      return std::bit_cast<double>(literal(std::bit_cast<std::uint64_t>(value)));
    } else {
      using Bits = std::make_unsigned_t<T>;
      auto bits = static_cast<std::uint64_t>(std::bit_cast<Bits>(value));
      if (loading_) {
        require(sizeof(T) <= input_.size() - position_);
        bits = 0;
        for (unsigned byte = 0; byte < sizeof(T); ++byte)
          bits |= std::uint64_t(input_[position_++]) << (byte * 8);
      } else {
        for (unsigned byte = 0; byte < sizeof(T); ++byte)
          output_.push_back(static_cast<std::uint8_t>(bits >> (byte * 8)));
      }
      return std::bit_cast<T>(static_cast<Bits>(bits));
    }
  }

  template <typename T> T field(T &value) {
    const auto decoded = literal(loading_ ? T{} : value);
    defer([&value, decoded] { value = decoded; });
    return decoded;
  }

  template <typename T> void identity(T expected) {
    require(literal(expected) == expected);
  }

  template <typename T> T bounded(T &value, T minimum, T maximum) {
    const auto decoded = literal(loading_ ? T{} : value);
    require(decoded >= minimum && decoded <= maximum);
    defer([&value, decoded] { value = decoded; });
    return decoded;
  }

  template <typename... T> void fields(T &...values) {
    (field(values), ...);
  }

  template <typename T, std::size_t Size> void array(std::array<T, Size> &values) {
    if constexpr (std::is_same_v<T, bool>) {
      auto decoded = values;
      for (auto &value : decoded)
        value = literal(value);
      defer([&values, decoded] { values = decoded; });
    } else {
      span(std::span<T>(values));
    }
  }

  template <typename T, std::size_t Extent> void span(std::span<T, Extent> values) {
    if (loading_) {
      require(values.size() <= (input_.size() - position_) / sizeof(T));
      auto decoded = std::make_shared<std::vector<T>>(values.size());
      for (auto &value : *decoded)
        value = literal(T{});
      defer([values, decoded] { std::copy(decoded->begin(), decoded->end(), values.begin()); });
    } else {
      for (const auto value : values)
        literal(value);
    }
  }

  template <typename T> void fixed_vector(std::vector<T> &values, std::string_view name = {}) {
    if (!name.empty())
      member_label(name, "size", 4);
    identity(static_cast<std::uint32_t>(values.size()));
    if (!name.empty())
      member_label(name, "data", sizeof(T));
    span(std::span(values));
  }

  template <typename T>
  void indexed_fixed_vector(std::vector<T> &values, std::string_view name, std::size_t index,
                            std::string_view member) {
    if (describe_)
      fixed_vector(values,
                   std::string(name) + "[" + std::to_string(index) + "]." + std::string(member));
    else
      fixed_vector(values);
  }

  template <typename T> std::uint32_t vector(std::vector<T> &values, std::uint32_t maximum) {
    const auto size = literal(static_cast<std::uint32_t>(values.size()));
    require(size <= maximum);
    if (loading_) {
      require(size <= (input_.size() - position_) / sizeof(T));
      auto decoded = std::make_shared<std::vector<T>>(size);
      for (auto &value : *decoded)
        value = literal(T{});
      defer([&values, decoded] { values.swap(*decoded); });
    } else {
      span(std::span(values));
    }
    return size;
  }

  template <typename T>
  std::vector<T> owned_vector(const std::vector<T> &values, std::uint32_t maximum,
                              std::string_view name = {}) {
    if (!name.empty())
      member_label(name, "size", 4);
    const auto size = literal(static_cast<std::uint32_t>(values.size()));
    if (!name.empty())
      member_label(name, "data", sizeof(T));
    require(size <= maximum);
    if (loading_) {
      require(size <= (input_.size() - position_) / sizeof(T));
      std::vector<T> decoded(size);
      for (auto &value : decoded)
        value = literal(T{});
      return decoded;
    }
    for (const auto value : values)
      literal(value);
    return values;
  }

  void bytes_identity(std::span<const std::uint8_t> bytes) {
    identity(static_cast<std::uint64_t>(bytes.size()));
    identity(fingerprint(bytes));
  }
  void defer(std::function<void()> mutation) {
    if (loading_)
      mutations_.push_back(std::move(mutation));
  }
  std::vector<std::uint8_t> finish();
  void validate() const;

private:
  void close_range() {
    if (!ranges_.empty())
      ranges_.back().bytes = output_.size() - ranges_.back().offset;
  }
  bool describe_ = false;
  std::vector<StateRange> ranges_;
  bool loading_ = false;
  std::span<const std::uint8_t> input_;
  std::size_t position_ = 0;
  std::vector<std::uint8_t> output_;
  std::vector<std::function<void()>> mutations_;
};

} // namespace cupid::n64::state
