#pragma once
#include "renderer/video/frame.hpp"
#include <chrono>
#include <filesystem>
#include <vector>

namespace cupid::desktop {
class FieldTrace {
public:
  using Clock = std::chrono::steady_clock;
  struct Observation {
    unsigned field;
    std::uint64_t clocks, pc, audio;
    bool audio_ready, gpu_ready;
    std::uint64_t audio_wait_ns = 0;
  };
  FieldTrace(std::filesystem::path trace, std::filesystem::path captures,
             const std::filesystem::path &capture_fields, unsigned requested);
  void begin();
  void advance(std::chrono::nanoseconds duration);
  void paint(std::chrono::nanoseconds duration);
  void ready(Clock::time_point completed = Clock::now());
  void presented(Observation observation);
  bool captures(unsigned field) const;
  void capture(unsigned field, const n64::VideoFrame &frame);
  void finish(bool complete);

private:
  struct Record {
    Observation observation;
    double elapsed, total, advance, paint, wait;
  };
  struct Capture {
    unsigned field;
    n64::VideoFrame frame;
  };
  std::filesystem::path trace_, capture_prefix_;
  unsigned requested_;
  std::vector<unsigned> capture_fields_;
  std::vector<Record> records_;
  std::vector<Capture> images_;
  Clock::time_point origin_{}, previous_{}, ready_{};
  std::chrono::nanoseconds advance_{}, paint_{};
  std::uint64_t previous_audio_wait_ = 0;
};
} // namespace cupid::desktop
