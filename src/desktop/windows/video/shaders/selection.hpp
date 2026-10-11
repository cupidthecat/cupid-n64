#pragma once

#include <filesystem>

namespace cupid::desktop {

enum class DisplayShader { None, Soft, Scanlines, File };

struct ShaderSelection {
  DisplayShader preset = DisplayShader::None;
  std::filesystem::path file;

  bool valid() const {
    return preset >= DisplayShader::None && preset <= DisplayShader::File &&
           (preset != DisplayShader::File || !file.empty());
  }
  bool operator==(const ShaderSelection &) const = default;
};

ShaderSelection read_shader_selection(const std::filesystem::path &settings);
void save_shader_selection(const std::filesystem::path &settings, const ShaderSelection &selection);

} // namespace cupid::desktop
