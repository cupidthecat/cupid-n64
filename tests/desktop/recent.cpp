#include "desktop/storage/recent.hpp"
#include <chrono>
#include <fstream>
#include <iostream>

namespace {
unsigned checks = 0, failures = 0;
void check(bool condition) {
  ++checks;
  failures += !condition;
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    auto number = std::chrono::steady_clock::now().time_since_epoch().count();
    do {
      path = std::filesystem::temp_directory_path() / ("cupid-recent-" + std::to_string(number++));
    } while (!std::filesystem::create_directory(path));
  }
  ~Directory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};
} // namespace

int main() {
  using namespace cupid::desktop;
  Directory folder;
  RecentRoms recent;
  std::vector<std::filesystem::path> paths;
  for (unsigned n = 0; n < 12; ++n) {
    paths.push_back(folder.path / ("game-" + std::to_string(n) + ".z64"));
    std::ofstream(paths.back()) << n;
    recent.remember(paths.back());
  }
  check(recent.paths().size() == RecentRoms::limit);
  check(recent.paths().front() == paths[11] && recent.paths().back() == paths[2]);
  recent.remember(folder.path / "." / "game-5.z64");
  check(recent.paths().front() == paths[5] && recent.paths().size() == RecentRoms::limit);
  check(recent.paths()[1] == paths[11]);
  recent.remember(folder.path);
  recent.remember(folder.path / "missing.z64");
  check(recent.paths().front() == paths[5] && recent.paths().size() == RecentRoms::limit);
  const auto retained = recent.paths();
  RecentRoms restarted;
  restarted.restore(retained);
  check(restarted.paths() == retained);
  std::filesystem::remove(paths[5]);
  check(recent.prune());
  check(recent.paths().front() == paths[11] && recent.paths().size() == RecentRoms::limit - 1);
  check(!recent.prune());
  recent.clear();
  check(recent.paths().empty());
  recent.restore({paths[1], paths[1], paths[5], folder.path, paths[0]});
  check(recent.paths().size() == 2 && recent.paths()[0] == paths[1] &&
        recent.paths()[1] == paths[0]);
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
