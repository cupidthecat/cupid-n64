#pragma once

#include <functional>
#include <memory>

namespace cupid::n64 {
class Debugger;
}

namespace cupid::desktop {

class GdbServer {
public:
  GdbServer();
  ~GdbServer();
  GdbServer(const GdbServer &) = delete;
  GdbServer &operator=(const GdbServer &) = delete;
  void listen(unsigned port);
  void close();
  void disconnect();
  void target(n64::Debugger *debugger, std::function<void()> memory_fence = {});
  void poll();
  unsigned port() const;
  bool connected() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace cupid::desktop
