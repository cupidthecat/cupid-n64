#include "desktop/debug/gdb/protocol.hpp"
#include "desktop/debug/gdb/codec.hpp"
#include <stdexcept>
#include <utility>

namespace cupid::desktop {

GdbProtocol::GdbProtocol(n64::Debugger &debugger, std::function<void()> memory_fence)
    : debugger_(debugger), memory_fence_(std::move(memory_fence)) {
  debugger_.attach();
}

GdbProtocol::~GdbProtocol() {
  debugger_.detach();
}

void GdbProtocol::append(std::string_view bytes) {
  if (bytes.size() > 131072 - output_.size())
    throw std::runtime_error("The debugger is not reading its replies.");
  output_ += bytes;
}

void GdbProtocol::reply(std::string_view payload) {
  last_reply_ = gdb::packet(payload);
  append(last_reply_);
}

std::string GdbProtocol::take_output() {
  return std::exchange(output_, {});
}

std::string GdbProtocol::stop_reply() const {
  const auto stop = debugger_.stop();
  auto result = std::string(stop.kind == n64::DebugStopKind::Interrupt ? "T02" : "T05") +
                "thread:1;25:" + gdb::hex(debugger_.read_register(37).value_or(stop.pc), 16) + ";";
  if (stop.kind == n64::DebugStopKind::ReadWatch)
    result += "rwatch:" + gdb::hex(stop.address) + ";";
  if (stop.kind == n64::DebugStopKind::WriteWatch)
    result += "watch:" + gdb::hex(stop.address) + ";";
  if (stop.kind == n64::DebugStopKind::AccessWatch)
    result += "awatch:" + gdb::hex(stop.address) + ";";
  return result;
}

void GdbProtocol::notify_stop() {
  if (awaiting_stop_ && debugger_.halted()) {
    awaiting_stop_ = false;
    reply(stop_reply());
  }
}

void GdbProtocol::receive(std::string_view bytes) {
  for (const auto byte : bytes) {
    if (closing_)
      return;
    if (input_ == Input::Idle) {
      if (byte == '$') {
        payload_.clear();
        checksum_ = 0;
        escaped_ = false;
        input_ = Input::Payload;
      } else if (byte == '-' && !no_ack_)
        append(last_reply_);
      else if (byte == '\x03') {
        debugger_.interrupt();
        awaiting_stop_ = false;
        reply(stop_reply());
      }
    } else if (input_ == Input::Payload) {
      if (byte == '#' && !escaped_)
        input_ = Input::ChecksumHigh;
      else if (byte == '$' && !escaped_) {
        payload_.clear();
        checksum_ = 0;
      } else {
        checksum_ = (checksum_ + static_cast<unsigned char>(byte)) & 255;
        escaped_ = !escaped_ && byte == '}';
        if (payload_.size() == maximum_packet)
          throw std::runtime_error("The debugger sent an oversized packet.");
        payload_ += byte;
      }
    } else {
      const auto value = gdb::digit(byte);
      if (value < 0) {
        if (!no_ack_)
          append("-");
        input_ = Input::Idle;
      } else if (input_ == Input::ChecksumHigh) {
        received_checksum_ = static_cast<unsigned>(value) << 4;
        input_ = Input::ChecksumLow;
      } else {
        input_ = Input::Idle;
        received_checksum_ |= static_cast<unsigned>(value);
        const auto payload = gdb::unescape(payload_, maximum_packet);
        if (checksum_ != received_checksum_ || !payload) {
          if (!no_ack_)
            append("-");
          continue;
        }
        if (!no_ack_)
          append("+");
        command(*payload);
      }
    }
  }
}

} // namespace cupid::desktop
