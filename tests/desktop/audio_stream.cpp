#include "../support/test.hpp"
#include "desktop/audio/output.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

int main() {
  using namespace cupid::desktop;
  auto output = make_audio_output({});
  test::equal(output->available(), true);
  test::equal(output->floating_samples(), true);
  if (!output->available()) {
    std::wcerr << output->error() << '\n';
    return 1;
  }
  Audio::Settings settings;
  settings.latency = 20;
  output->configure(settings);
  std::vector<float> samples(48000 * 2, 1e-8f);
  output->submit_float(samples);
  std::atomic<bool> interrupted = false;
  output->wait_interrupt([&] { return interrupted.load(); });
  auto waiting = std::async(std::launch::async, [&] { return output->wait(0); });
  test::equal(waiting.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout, true);
  const auto started = std::chrono::steady_clock::now();
  interrupted = true;
  test::equal(waiting.get(), false);
  test::equal(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(100), true);
  test::equal(output->available(), true);
  test::equal(output->recovery_pending(), false);
  output->clear();
  interrupted = false;
  test::equal(output->wait(0), true);
  std::array<short, 120> pending{};
  pending[0] = 1;
  pending[1] = -1;
  output->submit(0, pending);
  test::equal(output->available(), true);
  output->clear();
  test::equal(output->recover(), false);
  output->wait_interrupt({});

  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
