#!/usr/bin/env bash
set -euo pipefail

sudo apt-get update
sudo apt-get install --yes libvulkan1 mesa-vulkan-drivers

shopt -s nullglob
drivers=(/usr/share/vulkan/icd.d/lvp_icd*.json)
if [[ ${#drivers[@]} -eq 0 ]]; then
  printf 'The lavapipe Vulkan driver manifest is missing.\n' >&2
  exit 1
fi
printf 'VK_DRIVER_FILES=%s\n' "${drivers[0]}" >> "$GITHUB_ENV"
