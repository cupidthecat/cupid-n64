#pragma once

#include <cstdint>
#include <memory>

namespace cupid::n64 {

class Rsp;

class RspCompiler {
public:
  explicit RspCompiler(Rsp &rsp);
  ~RspCompiler();
  bool run();
  void reset();
  void invalidate(std::uint32_t address, unsigned bytes);
  void expose_memory();

private:
  friend class CoreState;
  struct Impl;
  struct Emitter;
  std::unique_ptr<Impl> impl_;
};

} // namespace cupid::n64
