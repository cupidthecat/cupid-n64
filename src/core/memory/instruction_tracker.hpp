#pragma once

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <span>
#include <vector>

namespace cupid::n64 {

class InstructionTracker {
public:
  explicit InstructionTracker(std::uint32_t bytes)
      : sections_((std::uint64_t(bytes) + 4095) / 4096) {}

  bool fully_tracked() const {
    return fully_tracked_;
  }
  void set_fully_tracked(bool value) {
    fully_tracked_ = value;
  }

  std::uint64_t generation(std::uint32_t address) const {
    return address / 4096u < sections_.size() ? sections_[address / 4096u].generation : 0;
  }

  void watch(std::uint32_t address, std::uint32_t bytes) {
    visit(address, bytes, [](Section &section, unsigned first, unsigned last) {
      for (unsigned line = first; line <= last; ++line)
        section.lines.set(line);
    });
  }

  void invalidate(std::uint32_t address, std::uint32_t bytes) {
    visit(address, bytes, [](Section &section, unsigned first, unsigned last) {
      for (unsigned line = first; line <= last; ++line)
        if (section.lines.test(line)) {
          ++section.generation;
          break;
        }
    });
  }

  void invalidate_all() {
    for (auto &section : sections_)
      ++section.generation;
  }

private:
  struct Section {
    std::bitset<128> lines;
    std::uint64_t generation = 1;
  };
  template <typename Function>
  void visit(std::uint32_t address, std::uint32_t bytes, Function function) {
    const auto end =
        std::min<std::uint64_t>(std::uint64_t(address) + bytes, sections_.size() * 4096ull);
    for (std::uint64_t cursor = address; cursor < end;) {
      const auto next = std::min<std::uint64_t>(end, (cursor | 4095ull) + 1);
      function(sections_[cursor / 4096], unsigned(cursor & 4095) / 32,
               unsigned((next - 1) & 4095) / 32);
      cursor = next;
    }
  }

  std::vector<Section> sections_;
  bool fully_tracked_ = true;
};

} // namespace cupid::n64
