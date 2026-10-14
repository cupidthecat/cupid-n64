#include "desktop/audio.hpp"
#include "../../support/test.hpp"
#include "host.hpp"
#include <chrono>
#include <stdexcept>
#include <thread>

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
  first.frequency(48000);
  for (unsigned sample = 0; sample < 12000; ++sample)
    first.sample({1e-8, -1e-8});
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  first.clear();
}

void linux_audio_recovery_tests() {
  using cupid::desktop::Audio;
  Audio audio;
  auto settings = audio.settings();
  settings.device = L"CupidTestPreferred";
  audio.configure(settings);
  test::equal(audio.available(), true);
  test::equal(audio.using_fallback(), false);
  test::equal(audio.device_name() == settings.device, true);
  const auto exchange = [](const char *phase) {
    std::cout << phase << std::endl;
    std::string reply;
    if (!std::getline(std::cin, reply) || reply != phase)
      throw std::runtime_error("The audio recovery test lost its host connection.");
  };
  const auto until = [&audio](auto ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
      audio.probe();
      if (audio.recovery_pending())
        audio.recover();
      if (ready())
        return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < deadline);
    std::wcerr << L"Audio recovery timed out: " << audio.device_name() << L", available="
               << audio.available() << L", fallback=" << audio.using_fallback() << L", error="
               << audio.error() << L'\n';
    for (const auto &device : audio.devices())
      std::wcerr << L"Playback endpoint: " << device.name << L'\n';
    return false;
  };
  exchange("remove");
  test::equal(until([&] { return audio.available() && audio.using_fallback(); }), true);
  test::equal(audio.settings().device == settings.device, true);
  exchange("restore");
  test::equal(until([&] { return audio.available() && !audio.using_fallback(); }), true);
  test::equal(audio.device_name() == settings.device, true);
  settings.device.clear();
  audio.configure(settings);
  exchange("default");
  test::equal(until([&] { return audio.device_name() == L"CupidTestPreferred"; }), true);
  test::equal(audio.using_fallback(), false);
  test::equal(audio.available(), true);
}
