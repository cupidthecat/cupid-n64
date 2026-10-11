#pragma once

#include <filesystem>
#include <vector>

namespace cupid::desktop {

class RecentRoms {
public:
  static constexpr unsigned limit = 10;
  void restore(std::vector<std::filesystem::path> paths);
  void remember(const std::filesystem::path &path);
  bool prune();
  void clear();
  const std::vector<std::filesystem::path> &paths() const {
    return paths_;
  }

private:
  std::vector<std::filesystem::path> paths_;
};

} // namespace cupid::desktop
