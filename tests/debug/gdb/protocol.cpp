#include "desktop/debug/gdb/protocol.hpp"
#include "../fixture.hpp"
#include "desktop/debug/gdb/codec.hpp"
#include "desktop/debug/gdb/server.hpp"
#include "desktop/debug/gdb/transport/socket.hpp"
#include <chrono>
#include <thread>

namespace {
using namespace debug_test;
using namespace cupid::desktop;

std::string request(GdbProtocol &protocol, std::string_view command) {
  const auto framed = gdb::packet(command);
  for (auto byte : framed)
    protocol.receive(std::string_view(&byte, 1));
  return protocol.take_output();
}

void framing_tests() {
  auto console = machine(131);
  Debugger debugger(*console);
  GdbProtocol protocol(debugger);
  const auto before = CoreState::capture(*console);
  equal(request(protocol, "qAttached") == "+" + gdb::packet("1"), true);
  protocol.receive("$qAttached#00");
  equal(protocol.take_output() == "-", true);
  const auto reply = request(protocol, "qC");
  equal(reply == "+" + gdb::packet("QC1"), true);
  protocol.receive("-");
  equal(protocol.take_output() == gdb::packet("QC1"), true);
  equal(request(protocol, "qXfer:features:read:target.xml:0,5") == "+" + gdb::packet("m<?xml"),
        true);
  equal(request(protocol, "qXfer:features:read:target.xml:ffff,5") == "+" + gdb::packet("l"), true);
  equal(request(protocol, "qXfer:features:read:target.xml:0,0") == "+" + gdb::packet("E01"), true);
  equal(request(protocol, "QStartNoAckMode") == "+" + gdb::packet("OK"), true);
  equal(request(protocol, "qAttached") == gdb::packet("1"), true);
  protocol.receive("$qAttached#00");
  equal(protocol.take_output().empty(), true);
  equal(request(protocol, "unknown") == gdb::packet(""), true);
  equal(CoreState::capture(*console) == before, true);
  const std::string binary = std::string("$#}*") + '\0';
  const auto framed = gdb::packet(binary);
  equal(gdb::unescape(std::string_view(framed).substr(1, framed.size() - 4), 20).value_or("bad") ==
            binary,
        true);
  equal(gdb::unescape("}", 20).has_value(), false);
  equal(gdb::unescape("* ", 20).has_value(), false);
  equal(gdb::unescape("a* ", 20).value_or("") == "aaaa", true);
  equal(gdb::unescape("a*~", 20).has_value(), false);
}

void register_and_memory_tests() {
  auto console = machine(131);
  Debugger debugger(*console);
  unsigned fences = 0;
  GdbProtocol protocol(debugger, [&] { ++fences; });
  equal(request(protocol, "p25") == "+" + gdb::packet("ffffffff80001000"), true);
  equal(request(protocol, "P7=1122334455667788") == "+" + gdb::packet("OK"), true);
  equal(console->cpu().state().gpr[7], 0x1122334455667788);
  const auto before = CoreState::capture(*console);
  equal(request(protocol, "P25=ffffffff80001001") == "+" + gdb::packet("E01"), true);
  equal(request(protocol, "P7=1234") == "+" + gdb::packet("E01"), true);
  equal(request(protocol, "p47") == "+" + gdb::packet("E01"), true);
  equal(request(protocol, "m80001000,1001") == "+" + gdb::packet("E01"), true);
  equal(request(protocol, "M80002000,2:aa") == "+" + gdb::packet("E01"), true);
  equal(request(protocol, "m80001000,0") == "+" + gdb::packet(""), true);
  equal(fences, 1);
  equal(CoreState::capture(*console) == before, true);
  std::string registers;
  for (unsigned n = 0; n < Debugger::register_count; ++n)
    registers += gdb::hex(debugger.read_register(n).value_or(0), 16);
  equal(request(protocol, "g") == "+" + gdb::packet(registers), true);
  registers.replace(7 * 16, 16, "000000000000002a");
  registers.replace(37 * 16, 16, "ffffffff80001001");
  equal(request(protocol, "G" + registers) == "+" + gdb::packet("E01"), true);
  equal(CoreState::capture(*console) == before, true);
  registers.replace(37 * 16, 16, "ffffffff80001000");
  equal(request(protocol, "G" + registers) == "+" + gdb::packet("OK"), true);
  equal(console->cpu().state().gpr[7], 42);
  equal(request(protocol, "M80002000,4:11223344") == "+" + gdb::packet("OK"), true);
  equal(request(protocol, "m80002000,4") == "+" + gdb::packet("11223344"), true);
  const std::string binary = std::string("X80002000,5:$#}*") + '\0';
  equal(request(protocol, binary) == "+" + gdb::packet("OK"), true);
  equal(request(protocol, "m80002000,5") == "+" + gdb::packet("24237d2a00"), true);
  equal(request(protocol, "M80002000,1:zz") == "+" + gdb::packet("E01"), true);
  const auto pc = console->cpu().state().pc;
  equal(request(protocol, "s80001001") == "+" + gdb::packet("E01"), true);
  equal(console->cpu().state().pc, pc);
}

void execution_tests() {
  auto console = machine(131);
  const std::array program{test::i(9, 7, 7, 1), test::i(9, 7, 7, 1), 12u};
  code(*console, program);
  Debugger debugger(*console);
  GdbProtocol protocol(debugger);
  equal(request(protocol, "Z0,80001004,4") == "+" + gdb::packet("OK"), true);
  equal(request(protocol, "c") == "+", true);
  debugger.advance(1000);
  protocol.notify_stop();
  equal(protocol.take_output() == gdb::packet("T05thread:1;25:ffffffff80001004;"), true);
  equal(console->cpu().state().gpr[7], 1);
  protocol.notify_stop();
  equal(protocol.take_output().empty(), true);
  equal(request(protocol, "vCont;s:1") == "+", true);
  debugger.advance(1000);
  protocol.notify_stop();
  equal(protocol.take_output() == gdb::packet("T05thread:1;25:ffffffff80001008;"), true);
  equal(console->cpu().state().gpr[7], 2);
  equal(request(protocol, "z0,80001004,4") == "+" + gdb::packet("OK"), true);
  equal(request(protocol, "c") == "+", true);
  protocol.receive("\x03");
  equal(protocol.take_output() == gdb::packet("T02thread:1;25:ffffffff80001008;"), true);
  equal(request(protocol, "vCont;c:2") == "+" + gdb::packet("E01"), true);
  equal(request(protocol, "Z0,80001001,4") == "+" + gdb::packet("E01"), true);
  equal(request(protocol, "Z4,80002000,0") == "+" + gdb::packet("E01"), true);
  equal(request(protocol, "D") == "+" + gdb::packet("OK"), true);
  equal(protocol.closing(), true);
}

gdb::Socket connect(GdbServer &server) {
  const auto client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  equal(client != gdb::invalid_socket, true);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<std::uint16_t>(server.port()));
  equal(::connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0, true);
  equal(gdb::nonblocking(client), true);
  for (unsigned n = 0; n < 200 && !server.connected(); ++n) {
    server.poll();
    if (!server.connected())
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  equal(server.connected(), true);
  return client;
}

std::string exchange(GdbServer &server, gdb::Socket client, std::string_view payload) {
  const auto framed = gdb::packet(payload);
  equal(send(client, framed.data(), static_cast<int>(framed.size()), 0) ==
            static_cast<int>(framed.size()),
        true);
  std::string result;
  for (unsigned n = 0; n < 200; ++n) {
    server.poll();
    std::array<char, 4096> bytes{};
    const auto count = recv(client, bytes.data(), static_cast<int>(bytes.size()), 0);
    if (count > 0)
      result.append(bytes.data(), static_cast<std::size_t>(count));
    const auto end = result.find('#');
    if (end != result.npos && result.size() >= end + 3)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return result;
}

void server_tests() {
  auto console = machine(131);
  Debugger debugger(*console);
  GdbServer server;
  server.target(&debugger);
  server.listen(0);
  equal(server.port() != 0, true);
  GdbServer other;
  bool occupied = false;
  try {
    other.listen(server.port());
  } catch (const std::runtime_error &) {
    occupied = true;
  }
  equal(occupied, true);
  const auto before = CoreState::capture(*console);
  auto client = connect(server);
  equal(debugger.halted(), true);
  equal(exchange(server, client, "p25") == "+" + gdb::packet("ffffffff80001000"), true);
  equal(exchange(server, client, "D") == "+" + gdb::packet("OK"), true);
  equal(server.connected(), false);
  equal(debugger.attached(), false);
  equal(CoreState::capture(*console) == before, true);
  gdb::close_socket(client);
  client = connect(server);
  gdb::close_socket(client);
  for (unsigned n = 0; n < 200 && server.connected(); ++n) {
    server.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  equal(server.connected(), false);
  equal(debugger.attached(), false);
  equal(CoreState::capture(*console) == before, true);
  client = connect(server);
  server.target(nullptr);
  equal(server.connected(), false);
  equal(debugger.attached(), false);
  gdb::close_socket(client);
  server.close();
  equal(server.port(), 0);
}

} // namespace

int main() {
  framing_tests();
  register_and_memory_tests();
  execution_tests();
  server_tests();
  std::cout << test::checks << " checks, " << test::failures << " failures\n";
  return test::failures ? 1 : 0;
}
