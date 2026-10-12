#include "desktop/replay/field_trace.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace cupid::desktop {
FieldTrace::FieldTrace(std::filesystem::path trace, std::filesystem::path captures,
                       const std::filesystem::path &capture_fields, unsigned requested)
    : trace_(std::move(trace)), capture_prefix_(std::move(captures)), requested_(requested) {
  capture_prefix_.replace_extension();
  records_.reserve(std::min(requested, 262144u));
  if (!capture_fields.empty()) {
    std::ifstream fields(capture_fields);
    if (!fields)
      throw std::runtime_error("Could not open the capture field list.");
    std::int64_t field;
    while (fields >> field) {
      if (field <= 0 || static_cast<std::uint64_t>(field) > requested)
        throw std::runtime_error("Capture fields must be within the requested replay.");
      capture_fields_.push_back(static_cast<unsigned>(field));
    }
    if (!fields.eof())
      throw std::runtime_error("Could not read the capture field list.");
    std::sort(capture_fields_.begin(), capture_fields_.end());
    if (std::adjacent_find(capture_fields_.begin(), capture_fields_.end()) != capture_fields_.end())
      throw std::runtime_error("Capture fields must be unique.");
    images_.reserve(capture_fields_.size());
  }
}
void FieldTrace::begin() {
  origin_ = previous_ = Clock::now();
  ready_ = {};
  records_.clear();
  images_.clear();
  advance_ = paint_ = {};
  previous_audio_wait_ = 0;
  previous_renderer_ = 0;
  cpu_ = {};
  processor_ = ~0u;
}
void FieldTrace::advance(std::chrono::nanoseconds duration, std::chrono::nanoseconds cpu,
                         unsigned processor) {
  advance_ += duration;
  cpu_ += cpu;
  processor_ = processor;
}
void FieldTrace::paint(std::chrono::nanoseconds duration) {
  paint_ += duration;
}
void FieldTrace::ready(Clock::time_point completed) {
  ready_ = completed;
}
void FieldTrace::presented(Observation observation) {
  const auto now = Clock::now();
  const auto seconds = [](auto duration) {
    return std::chrono::duration<double>(duration).count();
  };
  records_.push_back({observation, seconds(now - origin_), seconds(now - previous_) * 1e6,
                      seconds(advance_) * 1e6, seconds(paint_) * 1e6,
                      ready_ == Clock::time_point{} ? 0 : seconds(now - ready_) * 1e6,
                      seconds(cpu_) * 1e6, processor_});
  records_.back().observation.audio_wait_ns -= previous_audio_wait_;
  previous_audio_wait_ = observation.audio_wait_ns;
  records_.back().observation.renderer_ns -= previous_renderer_;
  previous_renderer_ = observation.renderer_ns;
  previous_ = now;
  advance_ = paint_ = {};
  cpu_ = {};
  ready_ = {};
}
bool FieldTrace::captures(unsigned field) const {
  return std::binary_search(capture_fields_.begin(), capture_fields_.end(), field);
}
void FieldTrace::capture(unsigned field, const n64::VideoFrame &frame) {
  images_.push_back({field, frame});
}
void FieldTrace::finish(bool complete) {
  if (!trace_.empty()) {
    std::ofstream stream(trace_);
    stream << "field,clocks,pc,audio,elapsed_s,total_us,advance_us,paint_us,wait_us,audio_ready,"
              "gpu_ready,audio_wait_us,worker_cpu_us,worker_processor,renderer_us\n"
           << std::setprecision(12);
    for (const auto &r : records_) {
      const auto &f = r.observation;
      stream << f.field << ',' << f.clocks << ',' << f.pc << ',' << f.audio << ',' << r.elapsed
             << ',' << r.total << ',' << r.advance << ',' << r.paint << ',' << r.wait << ','
             << f.audio_ready << ',' << f.gpu_ready << ',' << f.audio_wait_ns / 1000.0 << ','
             << r.cpu << ',' << r.processor << ',' << f.renderer_ns / 1000.0 << '\n';
    }
    stream.close();
    if (!stream)
      throw std::runtime_error("Could not write the field trace.");
    auto report = trace_;
    report.replace_extension(".json");
    std::ofstream metadata(report);
    metadata << "{\"requested_fields\":" << requested_ << ",\"fields\":" << records_.size()
             << ",\"complete\":" << (complete ? "true" : "false")
             << ",\"elapsed_s\":" << std::setprecision(12)
             << (records_.empty() ? 0 : records_.back().elapsed)
             << ",\"captures\":" << images_.size() << "}\n";
    metadata.close();
    if (!metadata)
      throw std::runtime_error("Could not write the field trace report.");
  }
  for (const auto &c : images_) {
    auto path = capture_prefix_;
    path += "-" + std::to_string(c.field) + ".ppm";
    std::ofstream image(path, std::ios::binary);
    image << "P6\n" << c.frame.width << ' ' << c.frame.height << "\n255\n";
    for (std::size_t n = 0; n < c.frame.rgba.size(); n += 4)
      image.write(reinterpret_cast<const char *>(c.frame.rgba.data() + n), 3);
    image.close();
    if (!image)
      throw std::runtime_error("Could not write a replay capture.");
  }
}
} // namespace cupid::desktop
