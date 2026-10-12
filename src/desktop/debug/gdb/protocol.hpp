#pragma once

#include "core/debug/debugger.hpp"
#include <functional>
#include <string>
#include <string_view>

namespace cupid::desktop {

class GdbProtocol {
public:
  static constexpr unsigned maximum_packet = 16384;
  explicit GdbProtocol(n64::Debugger &debugger, std::function<void()> memory_fence = {});
  ~GdbProtocol();
  GdbProtocol(const GdbProtocol &) = delete;
  GdbProtocol &operator=(const GdbProtocol &) = delete;
  void receive(std::string_view bytes);
  void notify_stop();
  std::string take_output();
  bool closing() const {
    return closing_;
  }

private:
  enum class Input { Idle, Payload, ChecksumHigh, ChecksumLow };
  void command(std::string_view payload);
  bool query(std::string_view payload);
  void reply(std::string_view payload);
  void append(std::string_view bytes);
  std::string stop_reply() const;
  n64::Debugger &debugger_;
  std::function<void()> memory_fence_;
  std::string payload_, output_, last_reply_;
  Input input_ = Input::Idle;
  unsigned checksum_ = 0, received_checksum_ = 0;
  bool escaped_ = false, no_ack_ = false, awaiting_stop_ = false, closing_ = false;
};

} // namespace cupid::desktop
