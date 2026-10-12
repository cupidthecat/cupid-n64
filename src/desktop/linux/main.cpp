#include "desktop/linux/window.hpp"
#include <charconv>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
  try {
    cupid::desktop::Options options;
    std::filesystem::path settings = cupid::desktop::linux_host::settings_path();
    bool software = false;
    for (int n = 1; n < argc; ++n) {
      const std::string_view argument(argv[n]);
      if (argument == "--test-input") {
        options.test_input = true;
        continue;
      }
      if (argument == "--software") {
        software = true;
        continue;
      }
      if (n + 1 == argc)
        throw std::runtime_error("Missing command-line value.");
      const std::string_view value(argv[++n]);
      if (argument == "--rom")
        options.rom = value;
      else if (argument == "--pif")
        options.firmware = value;
      else if (argument == "--ipl")
        options.ipl = value;
      else if (argument == "--disk")
        options.disk = value;
      else if (argument == "--capture")
        options.capture = value;
      else if (argument == "--settings")
        settings = value;
      else if (argument == "--arcade") {
        const auto profile = cupid::n64::arcade_profile(value);
        if (!profile)
          throw std::runtime_error("Unknown Aleck64 game name.");
        options.arcade_profile = *profile;
      } else if (argument == "--frames") {
        const auto parsed =
            std::from_chars(value.data(), value.data() + value.size(), options.frames);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
            !options.frames)
          throw std::runtime_error("Frame count must be a positive integer.");
      } else
        throw std::runtime_error(
            "Use --rom FILE or --ipl FILE, --pif FILE, and optional --disk FILE, "
            "--arcade GAME, --frames N, --capture FILE, --test-input, "
            "--settings FILE, or --software.");
    }
    if (!options.frames && (options.test_input || !options.capture.empty()))
      throw std::runtime_error("--capture and --test-input require --frames N.");
    if (!options.disk.empty() && options.ipl.empty())
      throw std::runtime_error("--disk requires --ipl FILE.");
    if (!gtk_init_check(nullptr, nullptr))
      throw std::runtime_error("Could not open the desktop display.");
    cupid::desktop::linux_host::Window window(std::move(options), std::move(settings),
                                              software ? std::optional(false) : std::nullopt);
    return window.run();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
