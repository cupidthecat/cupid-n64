#include "../replay/fixture.hpp"
#include "logging.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>

namespace test {
namespace {
class CacheLog final : public Util::LoggingInterface {
public:
  unsigned initialized = 0;
  CacheLog() {
    Util::set_thread_logging_interface(this);
  }
  ~CacheLog() {
    Util::set_thread_logging_interface(nullptr);
  }
  bool log(const char *, const char *message, va_list) override {
    if (std::strstr(message, "Initializing pipeline cache."))
      ++initialized;
    return false;
  }
};
} // namespace

void gpu_pipeline_cache_tests() {
  using namespace renderer_replay;
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path() / ("cupid-pipeline-cache-" + std::to_string(stamp));
  if (!std::filesystem::create_directory(directory))
    throw std::runtime_error("The pipeline cache test needs a fresh directory.");
  const auto path = directory / "pipelines.bin";
  {
    CacheLog log;
    GpuFixture fixture(path);
    for (unsigned n = 0; n < 64 * 64 / 2; ++n)
      fixture.put(0x100000 + n * 4, 0xf80107c1);
    fixture.program(base_video(2));
    const auto expected = fixture.renderer->frame(false);
    equal(expected.width, 640);
    equal(expected.height, 240);
    equal(expected.rgba.empty(), false);
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
      fixture.power(true);
      equal(std::filesystem::exists(path), true);
      equal(std::filesystem::file_size(path) >= 24, true);
      equal(log.initialized, cycle + 1);
      fixture.program(base_video(2));
      equal(fixture.renderer->frame(false).rgba == expected.rgba, true);
      equal(fixture.console.ram().read(0x100000, 4), 0xf80107c1);
      equal(fixture.crashed(), false);
    }
  }
  std::error_code error;
  equal(std::filesystem::remove(path, error), true);
  equal(std::filesystem::remove(directory, error), true);
}

} // namespace test
