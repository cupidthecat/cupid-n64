#include "desktop/storage/recent.hpp"
#include <algorithm>

namespace cupid::desktop {

void RecentRoms::restore(std::vector<std::filesystem::path> paths) {
  paths_.clear();
  for (auto iterator = paths.rbegin(); iterator != paths.rend(); ++iterator)
    remember(*iterator);
}

void RecentRoms::remember(const std::filesystem::path &path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error))
    return;
  auto absolute = std::filesystem::absolute(path).lexically_normal();
  prune();
  std::erase_if(paths_, [&](const std::filesystem::path &entry) {
    std::error_code result;
    return entry == absolute || std::filesystem::equivalent(entry, absolute, result);
  });
  paths_.insert(paths_.begin(), std::move(absolute));
  if (paths_.size() > limit)
    paths_.resize(limit);
}

bool RecentRoms::prune() {
  const auto previous = paths_.size();
  std::erase_if(paths_, [](const std::filesystem::path &path) {
    std::error_code error;
    return !std::filesystem::is_regular_file(path, error);
  });
  return paths_.size() != previous;
}

void RecentRoms::clear() {
  paths_.clear();
}

} // namespace cupid::desktop
