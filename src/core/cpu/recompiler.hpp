#pragma once

#include <cstdint>
#include <memory>

namespace cupid::n64 {

class Cpu;

class CpuCompiler {
public:
  explicit CpuCompiler(Cpu &cpu);
  ~CpuCompiler();
  bool run(const std::uint64_t &clock_target);
  void reset();

private:
  friend class CoreState;
  struct Impl;
  struct Emitter;
  std::unique_ptr<Impl> impl_;
};

} // namespace cupid::n64
