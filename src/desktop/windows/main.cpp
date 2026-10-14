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
      else if (argument == L"--arcade") {
        std::string name;
        for (auto letter : value)
          name.push_back(static_cast<char>(letter));
        const auto profile = cupid::n64::arcade_profile(name);
        if (!profile)
          throw std::runtime_error("Unknown Aleck64 game name. Use generic, 11beat, starsldr, "
                                   "doncdoon, kurufev, mayjin3, vivdolls, twrshaft, hipai, "
                                   "hipai2, srmvs, srmvsa, or mtetrisc.");
        options.arcade_profile = *profile;
      } else if (argument == L"--dip") {
        std::string selection;
        for (auto letter : value)
          selection.push_back(static_cast<char>(letter));
        options.arcade_dips.push_back(cupid::desktop::parse_dip_selection(selection));
      } else if (argument == L"--capture")
        options.capture = value;
      else if (argument == L"--input-replay")
        options.input_replay = value;
      else if (argument == L"--field-trace")
        options.field_trace = value;
      else if (argument == L"--capture-fields")
        options.capture_fields = value;
      else if (argument == L"--frames") {
        std::size_t parsed = 0;
        const auto frames = std::stoul(value, &parsed);
        if (!frames || parsed != value.size() || value.front() == L'-')
          throw std::runtime_error("Frame count must be a positive integer.");
        options.frames = frames;
      } else
        throw std::runtime_error(
            "Use --rom FILE or --ipl FILE, --pif FILE, and optional --disk "
            "FILE, --arcade GAME, --dip NAME=VALUE, --frames N, --capture FILE, --test-input, "
            "--input-replay FILE, --field-trace FILE, --capture-fields FILE.");
    }
    if (!options.frames &&
        (options.test_input || !options.capture.empty() || !options.input_replay.empty() ||
         !options.field_trace.empty() || !options.capture_fields.empty()))
      throw std::runtime_error("Replay, trace, and capture options require --frames N.");
    if (!options.capture_fields.empty() && options.capture.empty())
      throw std::runtime_error("--capture-fields requires --capture FILE.");
    if (!options.disk.empty() && options.ipl.empty())
      throw std::runtime_error("--disk requires --ipl FILE.");
    if (options.arcade_profile != cupid::n64::ArcadeProfile::Disabled &&
        (options.rom.empty() || options.firmware.empty() || !options.ipl.empty() ||
         !options.disk.empty()))
      throw std::runtime_error("--arcade requires --rom FILE and --pif FILE.");
    LocalFree(arguments);
    arguments = nullptr;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    cupid::desktop::Window window(std::move(options));
    return window.run(instance, show);
  } catch (const std::exception &exception) {
    LocalFree(arguments);
    MessageBoxA(nullptr, exception.what(), "Cupid-N64", MB_OK | MB_ICONERROR);
    return 1;
  }
}
