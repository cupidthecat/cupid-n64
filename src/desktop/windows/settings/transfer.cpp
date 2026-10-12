#include "desktop/windows/settings/transfer.hpp"
#include "desktop/storage/save.hpp"
#include <stdexcept>
#include <windows.h>

namespace cupid::desktop {
namespace {

settings::Document current(const std::filesystem::path &path) {
  if (path.empty())
    throw std::runtime_error("The desktop settings folder is unavailable.");
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
  return std::filesystem::exists(path) ? settings::read_document(path, false)
                                       : settings::Document{};
}

void write(const std::filesystem::path &path, const settings::Document &document, bool exported) {
  const auto bytes = settings::encode_document(document, exported);
  write_save(path, bytes);
  if (!exported)
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
}

} // namespace

SettingsTransfer SettingsTransfer::capture(const std::filesystem::path &path) {
  return SettingsTransfer(settings::normalize(current(path), false));
}

SettingsTransfer SettingsTransfer::open(const std::filesystem::path &path) {
  return SettingsTransfer(settings::normalize(settings::read_document(path, true), true));
}

void SettingsTransfer::export_to(const std::filesystem::path &path) const {
  write(path, settings::normalize(document_, true), true);
}

void SettingsTransfer::import_to(const std::filesystem::path &path) const {
  auto incoming = settings::normalize(document_, true);
  for (auto &[section, entries] : current(path))
    if (!settings::supported_section(section))
      incoming.emplace(std::move(section), std::move(entries));
  write(path, incoming, false);
}

std::vector<settings::LocalPath> SettingsTransfer::missing_paths() const {
  std::vector<settings::LocalPath> result;
  for (const auto &field : settings::local_paths(document_)) {
    std::error_code error;
    const bool exists = field.kind == settings::PathKind::Folder
                            ? std::filesystem::is_directory(field.path, error)
                            : std::filesystem::is_regular_file(field.path, error);
    if (!exists || error)
      result.push_back(field);
  }
  return result;
}

void SettingsTransfer::path(const settings::LocalPath &field, const std::filesystem::path &path) {
  auto candidate = document_;
  candidate.at(field.section).at(field.key) = path.wstring();
  document_ = settings::normalize(candidate, true);
}

} // namespace cupid::desktop
