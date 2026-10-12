#include "desktop/audio.hpp"
#include "../../support/test.hpp"
#include "host.hpp"
#include <stdexcept>

void linux_audio_tests() {
  using cupid::desktop::Audio;
  const auto play = [](Audio &audio) {
    audio.frequency(32027);
    for (unsigned sample = 0; sample < 32027; ++sample) {
      audio.sample({0.25, -0.25});
      if (!audio.available())
        throw std::runtime_error("The playback device stopped while submitting audio.");
    }
  };
  Audio first;
  test::equal(first.available(), true);
  {
    Audio second;
    test::equal(second.available(), true);
    play(first);
    play(second);
    test::equal(first.available(), true);
    test::equal(second.available(), true);
    second.clear();
  }
  play(first);
  test::equal(first.available(), true);
  first.clear();
}
