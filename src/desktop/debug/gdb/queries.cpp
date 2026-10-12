#include "desktop/debug/gdb/codec.hpp"
#include "desktop/debug/gdb/protocol.hpp"
#include <algorithm>

namespace cupid::desktop {

bool GdbProtocol::query(std::string_view payload) {
  constexpr std::string_view xml =
      "<?xml version=\"1.0\"?><!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
      "<target version=\"1.0\"><architecture>mips:4000</architecture></target>";
  constexpr std::string_view prefix = "qXfer:features:read:target.xml:";
  if (payload.starts_with(prefix)) {
    const auto arguments = payload.substr(prefix.size());
    const auto comma = arguments.find(',');
    const auto offset = gdb::number(arguments.substr(0, comma));
    const auto length =
        comma == arguments.npos ? std::nullopt : gdb::number(arguments.substr(comma + 1));
    if (!offset || !length || !*length) {
      reply("E01");
      return true;
    }
    const auto start = static_cast<std::size_t>(std::min<std::uint64_t>(*offset, xml.size()));
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
        *length, std::min<std::size_t>(xml.size() - start, maximum_packet / 2)));
    reply(std::string(start + count == xml.size() ? "l" : "m") +
          std::string(xml.substr(start, count)));
  } else if (payload.starts_with("qSupported"))
    reply("PacketSize=" + gdb::hex(n64::Debugger::maximum_transfer) +
          ";swbreak+;hwbreak+;QStartNoAckMode+;qXfer:features:read+;vContSupported+");
  else if (payload == "QStartNoAckMode") {
    reply("OK");
    no_ack_ = true;
  } else if (payload.starts_with("QPassSignals:") || payload.starts_with("qSymbol:") ||
             payload == "Hc0" || payload == "Hc1" || payload == "Hc-1" || payload == "Hg0" ||
             payload == "Hg1" || payload == "Hg-1" || payload == "T1")
    reply("OK");
  else if (payload == "qAttached")
    reply("1");
  else if (payload == "qC")
    reply("QC1");
  else if (payload == "qfThreadInfo")
    reply("m1");
  else if (payload == "qsThreadInfo")
    reply("l");
  else if (payload == "qOffsets")
    reply("Text=0;Data=0;Bss=0");
  else if (payload == "vCont?")
    reply("vCont;c;s;t");
  else
    return false;
  return true;
}

} // namespace cupid::desktop
