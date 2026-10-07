#include "image.hpp"
#include "core/disk/drive.hpp"
#include "geometry.hpp"

namespace cupid::n64 {

bool DiskImage::load(std::span<const std::uint8_t> input) {
  data.clear();
  errors.clear();
  const bool logical = input.size() == disk_geometry::LogicalSize;
  const bool compact = input.size() >= 0x4f08 && input.size() < 0x3d79140;
  if (!logical && !compact && input.size() != disk_geometry::PhysicalSize)
    return false;
  if (!validate(input, logical, compact) || !map(input, logical, compact)) {
    data.clear();
    errors.clear();
    return false;
  }
  return true;
}

bool DiskDrive::load_image(std::span<const std::uint8_t> image) {
  DiskImage decoded;
  if (!decoded.load(image))
    return false;
  disk_ = std::move(decoded.data);
  errors_ = std::move(decoded.errors);
  status_ |= Changed | Present;
  return true;
}

} // namespace cupid::n64
