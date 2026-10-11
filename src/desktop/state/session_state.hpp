#pragma once

#include "core/state/archive.hpp"
#include <span>
#include <vector>

namespace cupid::desktop {

class AudioResampler;
class Session;

class SessionState {
public:
  static std::vector<std::uint8_t> capture(Session &session);
  static void restore(Session &session, std::span<const std::uint8_t> bytes);

private:
  static void resampler(n64::state::Archive &archive, AudioResampler &resampler);
};

} // namespace cupid::desktop
