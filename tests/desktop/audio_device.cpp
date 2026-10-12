#include "../support/test.hpp"
#include "desktop/audio/output.hpp"
#include "desktop/windows/audio/identity.hpp"
#include <array>

using namespace cupid::desktop;
int main() {
  const std::wstring id = L"{0.0.0.00000000}.{abcdefgh-0000-0000-0000-123456789012}";
  const auto legacy = L"\\\\?\\SWD#MMDEVAPI#" + id + L"#{device-interface-class}";
  test::equal(audio_endpoint_key(legacy) == id, true);
  test::equal(audio_endpoint_key(id) == id, true);
  test::equal(audio_endpoint_key(L"").empty(), true);
  test::equal(audio_endpoint_key(L"unavailable-device") == L"unavailable-device", true);
  test::equal(same_audio_endpoint(legacy, id), true);
  test::equal(same_audio_endpoint(L"{0.0.0.00000000}.{ABC}", L"{0.0.0.00000000}.{abc}"), true);
  test::equal(same_audio_endpoint(id, L"another-device"), false);
  test::equal(same_audio_endpoint(id, L""), false);

  // Construction and control operations join the audio thread's COM work.
  auto output = make_audio_output({});
  const auto initial = output->devices();
  output->recover();
  test::equal(output->devices().size(), initial.size());
  if (output->available()) {
    Audio::Settings settings;
    settings.latency = 20;
    output->configure(settings);
    test::equal(output->available(), true);
    test::equal(output->device_name().empty(), false);
    std::array<short, 960> silence{};
    for (unsigned n = 0; n < 12; ++n) {
      test::equal(output->wait(n), true);
      output->submit(n, silence);
    }
    output->clear();
    test::equal(output->available(), true);
    test::equal(output->wait(0), true);
    output->submit(0, std::span(silence).first(120));
    output->clear();
    settings.levels.volume = 50;
    output->configure(settings);
    test::equal(output->available(), true);
    test::equal(output->recover(), false);
    test::equal(output->recovery_pending(), false);
  } else {
    // Hosted Windows runners need not expose an audio endpoint.
    std::wcout << L"No active audio stream: " << output->error() << L'\n';
    output->clear();
    output->configure({});
    output->recover();
  }
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
