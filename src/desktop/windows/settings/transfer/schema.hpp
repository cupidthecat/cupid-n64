#pragma once

#include "desktop/windows/settings/transfer/document.hpp"

namespace cupid::desktop::settings {

enum class PathKind { Cartridge, Pif, Ipl, Disk, Folder, Handheld, Shader };

struct LocalPath {
  std::wstring section, key;
  std::filesystem::path path;
  PathKind kind;
};

Document normalize(const Document &document, bool complete);
bool supported_section(std::wstring_view section);
std::vector<LocalPath> local_paths(const Document &document);
int integer(std::wstring_view text);

} // namespace cupid::desktop::settings
