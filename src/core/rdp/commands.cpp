#include "core/rdp/rdp.hpp"

namespace cupid::n64 {

void Rdp::render() {
  constexpr std::array<unsigned, 64> lengths = {
      1, 1, 1, 1, 1, 1, 1, 1, 4, 6, 12, 14, 12, 14, 20, 22, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  2,  2,  1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,  1, 1, 1, 1};
  auto current = command_.current & ~7u;
  const auto end = command_.end & ~7u;
  if (current >= end)
    return;
  const auto words = (end - current) / 8;
  if (queue_size_ + words >= buffer_.size() / 2)
    return;
  for (unsigned n = 0; n < words; ++n) {
    for (unsigned half = 0; half < 2; ++half) {
      buffer_[queue_size_ * 2 + half] = static_cast<std::uint32_t>(
          command_.source ? rsp_.read_local(current & 0xfff, 4) : ram_.read(current, 4));
      current += 4;
    }
    ++queue_size_;
  }
  while (queue_offset_ < queue_size_) {
    const auto code = (buffer_[queue_offset_ * 2] >> 24) & 63;
    const auto length = lengths[code];
    if (queue_offset_ + length > queue_size_) {
      command_.start = command_.current = command_.end;
      return;
    }
    if (code >= 8 && submit_)
      submit_(std::span(buffer_).subspan(queue_offset_ * 2, length * 2));
    if (code == 0x29) {
      if (synchronize_)
        synchronize_();
      sync_full();
    }
    queue_offset_ += length;
  }
  queue_offset_ = queue_size_ = 0;
  command_.current = command_.end;
}

} // namespace cupid::n64
