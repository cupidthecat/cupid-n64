#pragma once

#include <algorithm>
#include <array>
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

  void capture(std::span<const std::uint32_t> memory) {
    snapshots_.clear();
    const auto count = std::min(sections_.size(), memory.size() / 1024);
    for (unsigned index = 0; index < count; ++index) {
      if (sections_[index].lines.none())
        continue;
      auto &snapshot = snapshots_.emplace_back();
      snapshot.index = index;
      snapshot.lines = sections_[index].lines;
      std::copy_n(memory.begin() + index * 1024, 1024, snapshot.words.begin());
    }
  }

  void compare(std::span<const std::uint32_t> memory) {
    for (const auto &snapshot : snapshots_) {
      for (unsigned line = 0; line < 128; ++line) {
        if (snapshot.lines.test(line) &&
            !std::equal(snapshot.words.begin() + line * 8, snapshot.words.begin() + (line + 1) * 8,
                        memory.begin() + snapshot.index * 1024 + line * 8)) {
          ++sections_[snapshot.index].generation;
          break;
        }
      }
    }
    snapshots_.clear();
  }

private:
  struct Section {
    std::bitset<128> lines;
    std::uint64_t generation = 1;
  };
  struct Snapshot {
    unsigned index;
    std::bitset<128> lines;
    std::array<std::uint32_t, 1024> words;
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
  std::vector<Snapshot> snapshots_;
  bool fully_tracked_ = true;
};

} // namespace cupid::n64
