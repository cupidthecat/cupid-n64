#include "../support/test.hpp"
#include "desktop/replay/field_trace.hpp"
#include "desktop/replay/input.hpp"
#include <fstream>
#include <sstream>
#include <thread>

using namespace cupid::desktop;
int main() {
  std::istringstream source("10 20 32768 -128 127\n\n25 30 4096 80 -80\r\n");
  InputReplay replay(source);
  test::equal(replay.at(9).buttons, 0);
  test::equal(replay.at(10).buttons, 32768);
  test::equal(replay.at(19).x, -128);
  test::equal(replay.at(19).y, 127);
  test::equal(replay.at(19).calibrated_stick, true);
  test::equal(replay.at(20).buttons, 0);
  test::equal(replay.at(25).buttons, 4096);
  test::equal(replay.at(30).x, 0);
  for (const auto invalid :
       {"-1 10 0 0 0", "0 0 0 0 0", "0 10 65536 0 0", "0 10 0 -129 0", "0 10 0 0 128", "0 10 0 0",
        "0 10 0 0 0 extra", "0 10 0 0 0\n5 20 0 0 0"}) {
    bool rejected = false;
    try {
      std::istringstream stream(invalid);
      InputReplay input(stream);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    test::equal(rejected, true);
  }
  const auto directory = std::filesystem::absolute(
      "playback-trace-test-" + std::to_string(FieldTrace::Clock::now().time_since_epoch().count()));
  if (!std::filesystem::create_directory(directory))
    return 1;
  const auto path = directory / "fields.csv";
  FieldTrace trace(path, {}, {}, 3);
  trace.begin();
  trace.advance(std::chrono::milliseconds(10), std::chrono::milliseconds(3), 4);
  trace.ready();
  const auto completed = FieldTrace::Clock::now();
  std::this_thread::sleep_for(std::chrono::milliseconds(4));
  trace.presented(
      {1, 100, 0xffffffff80001000, 20, true, true, 4000000, 2000000, true, true, 500000},
      completed);
  trace.advance(std::chrono::milliseconds(20), std::chrono::milliseconds(7), 6);
  trace.ready();
  trace.presented(
      {2, 200, 0xffffffff80001004, 40, true, true, 9000000, 7000000, true, false, 700000});
  trace.finish(false);
  std::ifstream csv(path), report(directory / "fields.json");
  const std::string text((std::istreambuf_iterator<char>(csv)), {});
  const std::string metadata((std::istreambuf_iterator<char>(report)), {});
  test::equal(text.find(",image_valid,video_presented,presentation_us,presentation_completed_s,"
                        "presentation_delivery_us\n") != std::string::npos,
              true);
  test::equal(text.find("1,100,18446744071562072064,20,") != std::string::npos, true);
  test::equal(metadata.find("\"complete\":false") != std::string::npos, true);
  test::equal(metadata.find("\"fields\":2") != std::string::npos, true);
  std::istringstream lines(text);
  std::string row;
  std::getline(lines, row);
  std::getline(lines, row);
  const auto columns = [](const std::string &text) {
    std::vector<std::string> result;
    std::istringstream input(text);
    std::string value;
    while (std::getline(input, value, ','))
      result.push_back(value);
    return result;
  };
  const auto first = columns(row);
  test::equal(first.size(), 20);
  test::equal(first[16] == "1" && first[17] == "500", true);
  test::equal(std::stod(first[19]) >= 3000, true);
  test::equal(std::stod(first[4]) > std::stod(first[18]), true);
  test::equal(std::stod(first[8]) >= std::stod(first[19]), true);
  std::getline(lines, row);
  const auto second = columns(row);
  test::equal(second[16] == "0" && second[17] == "700" && second[19] == "0", true);
  // The test owns only these newly created files.
  csv.close();
  report.close();
  std::filesystem::remove(path);
  std::filesystem::remove(directory / "fields.json");
  std::filesystem::remove(directory);
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
