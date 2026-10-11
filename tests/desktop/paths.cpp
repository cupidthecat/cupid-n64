#include "desktop/storage/paths.hpp"
#include "../support/test.hpp"
#include "desktop/storage/save.hpp"
#include <chrono>
#include <fstream>

int main() {
  using namespace cupid::desktop;
  const auto root = std::filesystem::temp_directory_path() /
                    ("cupid-storage-paths-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directory(root);
  try {
    StoragePaths paths;
    const auto image = root / "games" / "game.z64";
    const auto app = root / "application";
    test::equal(paths.save_image(image, 123) == image, true);
    for (auto location :
         {StorageLocation::Saves, StorageLocation::Screenshots, StorageLocation::Debugging})
      test::equal(paths.directory(location, image, app) == image.parent_path(), true);
    test::equal(paths.directory(StorageLocation::Firmware, image, app) == app, true);
    test::equal(paths.directory(StorageLocation::Screenshots, {}, app) == app, true);
    paths.saves = root / "saves";
    std::filesystem::create_directory(paths.saves);
    paths.screenshots = root / "images";
    paths.firmware = root / "firmware";
    paths.debugging = root / "reports";
    for (auto location : {StorageLocation::Saves, StorageLocation::Screenshots,
                          StorageLocation::Firmware, StorageLocation::Debugging})
      test::equal(paths.directory(location, image, app) == paths.selected(location), true);
    const auto first = paths.save_image(image, 123);
    test::equal(first.parent_path() == paths.saves, true);
    test::equal(first == paths.save_image(image, 123), true);
    test::equal(first == paths.save_image(root / "other" / image.filename(), 123), true);
    test::equal(first != paths.save_image(image, 456), true);
    test::equal(save_path(first, L".pak") != save_path(first, L".p2.pak"), true);
    const std::array<std::uint8_t, 4> bytes{1, 2, 3, 4};
    validate_directory(paths.saves, true);
    write_save(save_path(first, L".pak"), bytes);
    test::equal(read_file(save_path(first, L".pak"), bytes.size()) ==
                    std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
                true);
    test::equal(!std::filesystem::exists(save_path(image, L".pak")), true);
    const auto count = std::distance(std::filesystem::directory_iterator(paths.saves),
                                     std::filesystem::directory_iterator{});
    test::equal(count, 1);
    bool rejected = false;
    try {
      validate_directory(save_path(first, L".pak"), true);
    } catch (const std::exception &) {
      rejected = true;
    }
    test::equal(rejected, true);
    rejected = false;
    try {
      validate_directory(root / "missing", false);
    } catch (const std::exception &) {
      rejected = true;
    }
    test::equal(rejected, true);
    validate_directory(root, false);
    test::equal(StoragePaths{paths}.save_image({}, 123).empty(), true);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    ++test::failures;
  }
  std::filesystem::remove_all(root);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
