#include "desktop/windows/window.hpp"
#include <shellapi.h>
#include <stdexcept>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  int count = 0;
  auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  cupid::desktop::Options options;
  try {
    for (int index = 1; index < count; ++index) {
      const std::wstring argument = arguments[index];
      if (argument == L"--test-input") {
        options.test_input = true;
        continue;
      }
      if (index + 1 == count)
        throw std::runtime_error("Missing command-line value.");
      const std::wstring value = arguments[++index];
      if (argument == L"--rom")
        options.rom = value;
      else if (argument == L"--pif")
        options.firmware = value;
      else if (argument == L"--ipl")
        options.ipl = value;
      else if (argument == L"--disk")
        options.disk = value;
      else if (argument == L"--capture")
        options.capture = value;
      else if (argument == L"--frames") {
        std::size_t parsed = 0;
        const auto frames = std::stoul(value, &parsed);
        if (!frames || parsed != value.size() || value.front() == L'-')
          throw std::runtime_error("Frame count must be a positive integer.");
        options.frames = frames;
      } else
        throw std::runtime_error("Use --rom FILE or --ipl FILE, --pif FILE, and optional --disk "
                                 "FILE, --frames N, --capture FILE, --test-input.");
    }
    if (!options.frames && (options.test_input || !options.capture.empty()))
      throw std::runtime_error("--capture and --test-input require --frames N.");
    if (!options.disk.empty() && options.ipl.empty())
      throw std::runtime_error("--disk requires --ipl FILE.");
    LocalFree(arguments);
    arguments = nullptr;
    SetProcessDPIAware();
    cupid::desktop::Window window(std::move(options));
    return window.run(instance, show);
  } catch (const std::exception &exception) {
    LocalFree(arguments);
    MessageBoxA(nullptr, exception.what(), "Cupid-N64", MB_OK | MB_ICONERROR);
    return 1;
  }
}
