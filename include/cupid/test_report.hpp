#pragma once

#include "cupid/types.hpp"

#include <array>
#include <string>
#include <string_view>

namespace cupid {

class TestReport {
  public:
    void append(std::string_view bytes);
    void expect_extended();

    bool complete{};
    bool failed{};
    u64 tests{};

  private:
    std::string line_;
    std::array<bool, 5> categories_{};
    std::array<bool, 4> enabled_{};
    bool summary_{};
    bool require_extended_{};

    void consume_line();
};

} // namespace cupid
