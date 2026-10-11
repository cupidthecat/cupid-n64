#include "desktop/state/session_state.hpp"
#include "desktop/session.hpp"
#include <cmath>
#include <utility>

namespace cupid::desktop {
namespace {

void identity(n64::state::Archive &archive) {
  archive.identity<std::uint64_t>(0x4554415453535043ull);
  archive.identity<std::uint32_t>(1);
}

void frame_state(n64::state::Archive &archive, n64::VideoFrame &frame) {
  const auto width = archive.bounded(frame.width, 0u, 4096u);
  const auto height = archive.bounded(frame.height, 0u, 4096u);
  n64::state::Archive::require(bool(width) == bool(height));
  const auto size = archive.vector(frame.rgba, 4096 * 4096 * 4);
  n64::state::Archive::require(size == std::uint64_t(width) * height * 4);
}

void finite(n64::state::Archive &archive, double &value) {
  n64::state::Archive::require(std::isfinite(archive.field(value)));
}

} // namespace

void SessionState::resampler(n64::state::Archive &archive, AudioResampler &resampler) {
  for (auto &sample : resampler.history_) {
    finite(archive, sample.left);
    finite(archive, sample.right);
  }
  for (auto &filter : resampler.filters_) {
    finite(archive, filter.direct);
    finite(archive, filter.middle);
    finite(archive, filter.feedback1);
    finite(archive, filter.feedback2);
    finite(archive, filter.delay1.left);
    finite(archive, filter.delay1.right);
    finite(archive, filter.delay2.left);
    finite(archive, filter.delay2.right);
  }
  const auto count = archive.field(resampler.filter_count_);
  n64::state::Archive::require(count == 0 || count == 3);
  const auto ratio = archive.field(resampler.ratio_);
  const auto phase = archive.field(resampler.phase_);
  n64::state::Archive::require(std::isfinite(ratio) && ratio >= 1.0 / 48000 &&
                               ratio <= 50000000.0 / 48000 && std::isfinite(phase) && phase >= 0 &&
                               phase <= 50000000.0 / 48000);
}

std::vector<std::uint8_t> SessionState::capture(Session &session) {
  // Fence rendering before reading presentation and machine state.
  const auto machine = session.output_->capture_state();
  n64::state::Archive archive;
  identity(archive);
  archive.identity(session.disk_identity_);
  archive.field(session.frames);
  archive.field(session.frame_pending_);
  frame_state(archive, session.frame);
  resampler(archive, session.audio_.resampler_);
  archive.bounded(session.audio_.offset_, 0u, 958u);
  archive.span(std::span(session.audio_.buffers_[session.audio_.buffer_].samples)
                   .first(session.audio_.offset_));
  archive.owned_vector(machine, 448 * 1024 * 1024);
  return archive.finish();
}

void SessionState::restore(Session &session, std::span<const std::uint8_t> bytes) {
  n64::state::Archive archive(bytes);
  identity(archive);
  archive.identity(session.disk_identity_);
  archive.field(session.frames);
  archive.field(session.frame_pending_);
  frame_state(archive, session.frame);
  resampler(archive, session.audio_.resampler_);
  unsigned offset = 0;
  offset = archive.bounded(offset, 0u, 958u);
  n64::state::Archive::require((offset & 1) == 0);
  auto samples = std::make_unique<std::array<short, 960>>();
  archive.span(std::span(*samples).first(offset));
  const auto machine = archive.owned_vector<std::uint8_t>({}, 448 * 1024 * 1024);
  archive.validate();
  session.output_->restore_state(machine);
  session.audio_.clear();
  archive.finish();
  session.audio_.buffers_[0].samples = *samples;
  session.audio_.offset_ = offset;
}

} // namespace cupid::desktop
