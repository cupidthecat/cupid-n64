#include "../../support/test.hpp"
#include "host.hpp"
#include <string_view>

int main(int argc, char **argv) {
  try {
    const std::string_view mode = argc == 2 ? argv[1] : "";
    if (mode == "--settings")
      linux_settings_tests();
    else if (mode == "--input")
      linux_input_tests();
    else if (mode == "--audio")
      linux_audio_tests();
    else if (mode == "--audio-recovery")
      linux_audio_recovery_tests();
    else if (mode == "--window" || mode == "--hardware")
      linux_window_tests(mode == "--hardware");
    else
      throw std::runtime_error("Select a Linux frontend test mode.");
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
