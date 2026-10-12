#pragma once

#include "desktop/windows/settings/transfer/schema.hpp"

namespace cupid::desktop {

class SettingsTransfer {
public:
  static SettingsTransfer capture(const std::filesystem::path &settings);
  static SettingsTransfer open(const std::filesystem::path &exported);
  void export_to(const std::filesystem::path &path) const;
  void import_to(const std::filesystem::path &settings) const;
  std::vector<settings::LocalPath> missing_paths() const;
  void path(const settings::LocalPath &field, const std::filesystem::path &path);
  const settings::Document &document() const {
    return document_;
  }

private:
  explicit SettingsTransfer(settings::Document document) : document_(std::move(document)) {}
  settings::Document document_;
};

} // namespace cupid::desktop
