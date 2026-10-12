#include "desktop/debug/gdb/codec.hpp"
#include "desktop/debug/gdb/protocol.hpp"
#include <limits>

namespace cupid::desktop {

void GdbProtocol::command(std::string_view payload) {
  if (payload.empty()) {
    reply("");
    return;
  }
  if (query(payload))
    return;
  const auto opcode = payload.front();
  if (payload == "?") {
    reply(debugger_.halted() ? stop_reply() : "E01");
    return;
  }
  if (opcode == 'D' || payload == "k" || payload.starts_with("vKill;")) {
    reply("OK");
    closing_ = true;
    return;
  }
  if (opcode == 'c' || opcode == 's' || payload.starts_with("vCont;")) {
    bool step = opcode == 's';
    std::optional<std::uint64_t> pc;
    if (payload.starts_with("vCont;")) {
      const auto action = payload.substr(6);
      const auto colon = action.find(':');
      const auto kind = action.substr(0, colon);
      const auto thread = colon == action.npos ? std::string_view{} : action.substr(colon + 1);
      if ((kind != "c" && kind != "s" && kind != "t") ||
          (!thread.empty() && thread != "1" && thread != "0" && thread != "-1")) {
        reply("E01");
        return;
      }
      if (kind == "t") {
        debugger_.interrupt();
        awaiting_stop_ = false;
        reply(stop_reply());
        return;
      }
      step = kind == "s";
    } else if (payload.size() > 1) {
      pc = gdb::number(payload.substr(1));
      if (!pc || (*pc & 3)) {
        reply("E01");
        return;
      }
    }
    if (!debugger_.halted()) {
      reply("E01");
      return;
    }
    debugger_.resume(step, pc);
    awaiting_stop_ = true;
    return;
  }
  if (opcode == 'g' && payload.size() == 1) {
    std::string result;
    for (unsigned n = 0; n < n64::Debugger::register_count; ++n) {
      const auto value = debugger_.read_register(n);
      if (!value) {
        reply("E01");
        return;
      }
      result += gdb::hex(*value, 16);
    }
    reply(result);
    return;
  }
  if (opcode == 'p' || opcode == 'P') {
    const auto equals = payload.find('=');
    const auto index =
        gdb::number(payload.substr(1, equals == payload.npos ? payload.size() - 1 : equals - 1));
    if (!index || *index >= n64::Debugger::register_count) {
      reply("E01");
      return;
    }
    if (opcode == 'p' && equals == payload.npos) {
      const auto value = debugger_.read_register(static_cast<unsigned>(*index));
      reply(value ? gdb::hex(*value, 16) : "E01");
    } else if (opcode == 'P' && equals != payload.npos) {
      const auto text = payload.substr(equals + 1);
      const auto value = gdb::number(text);
      reply(text.size() == 16 && value &&
                    debugger_.write_register(static_cast<unsigned>(*index), *value)
                ? "OK"
                : "E01");
    } else
      reply("E01");
    return;
  }
  if (opcode == 'G') {
    const auto values = payload.substr(1);
    std::vector<std::uint64_t> registers;
    if (values.size() != n64::Debugger::register_count * 16 || !debugger_.halted()) {
      reply("E01");
      return;
    }
    for (unsigned n = 0; n < n64::Debugger::register_count; ++n) {
      const auto value = gdb::number(values.substr(n * 16, 16));
      if (!value || (n == 37 && (*value & 3))) {
        reply("E01");
        return;
      }
      registers.push_back(*value);
    }
    for (unsigned n = 0; n < registers.size(); ++n)
      debugger_.write_register(n, registers[n]);
    reply("OK");
    return;
  }
  if (opcode == 'm' || opcode == 'M' || opcode == 'X') {
    const auto comma = payload.find(','), colon = payload.find(':');
    const auto address =
        comma == payload.npos ? std::nullopt : gdb::number(payload.substr(1, comma - 1));
    const auto length = comma == payload.npos
                            ? std::nullopt
                            : gdb::number(payload.substr(comma + 1, colon == payload.npos
                                                                        ? payload.size() - comma - 1
                                                                        : colon - comma - 1));
    if (!address || !length || *length > n64::Debugger::maximum_transfer || !debugger_.halted() ||
        (opcode == 'm' ? colon != payload.npos : colon == payload.npos)) {
      reply("E01");
      return;
    }
    if (opcode == 'm') {
      if (memory_fence_)
        memory_fence_();
      const auto bytes = debugger_.read_memory(*address, static_cast<unsigned>(*length));
      reply(bytes ? gdb::encode(*bytes) : "E01");
      return;
    }
    const auto text = payload.substr(colon + 1);
    const auto bytes = opcode == 'M'
                           ? gdb::decode(text)
                           : std::optional(std::vector<std::uint8_t>(text.begin(), text.end()));
    if (!bytes || bytes->size() != *length) {
      reply("E01");
      return;
    }
    if (memory_fence_)
      memory_fence_();
    reply(debugger_.write_memory(*address, *bytes) ? "OK" : "E01");
    return;
  }
  if (opcode == 'Z' || opcode == 'z') {
    const auto first = payload.find(','),
               second = first == payload.npos ? payload.npos : payload.find(',', first + 1);
    const auto kind = gdb::number(payload.substr(1, first == payload.npos ? 0 : first - 1));
    const auto address = second == payload.npos
                             ? std::nullopt
                             : gdb::number(payload.substr(first + 1, second - first - 1));
    const auto length =
        second == payload.npos ? std::nullopt : gdb::number(payload.substr(second + 1));
    if (!kind || !address || !length || *kind > 4 ||
        *length > std::numeric_limits<unsigned>::max()) {
      reply("E01");
      return;
    }
    reply(debugger_.point(static_cast<n64::DebugPointKind>(*kind), *address,
                          static_cast<unsigned>(*length), opcode == 'Z')
              ? "OK"
              : "E01");
    return;
  }
  reply("");
}

} // namespace cupid::desktop
