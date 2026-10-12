#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace cupid::desktop {

enum class FileSelection {
  Cartridge,
  Pif,
  Ipl,
  Disk,
  Folder,
  Screenshot,
  Handheld,
  Shader,
  ImportSettings,
  ExportSettings
};

std::optional<std::filesystem::path> select_file(HWND owner, const wchar_t *title,
                                                 FileSelection kind,
                                                 const std::filesystem::path &directory,
                                                 const std::filesystem::path &current = {});

struct PathField {
  std::wstring label;
  std::filesystem::path path;
  FileSelection kind;
  bool enabled = true;
};

struct PathForm {
  std::wstring title, explanation;
  std::vector<PathField> fields;
  std::vector<std::wstring> choices;
  unsigned selected = 0;
  std::filesystem::path directory;
  std::function<void(PathForm &)> changed;
  std::function<void(const PathForm &)> validate;
};

std::optional<PathForm> choose_paths(HWND owner, PathForm form);

} // namespace cupid::desktop
