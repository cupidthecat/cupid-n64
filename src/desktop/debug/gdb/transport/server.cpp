#include "desktop/debug/gdb/server.hpp"
#include "desktop/debug/gdb/protocol.hpp"
#include "desktop/debug/gdb/transport/socket.hpp"
#include <array>
#include <stdexcept>
#include <utility>

namespace cupid::desktop {

struct GdbServer::Impl {
  gdb::Socket listener = gdb::invalid_socket, client = gdb::invalid_socket;
  n64::Debugger *debugger = nullptr;
  std::function<void()> memory_fence;
  std::unique_ptr<GdbProtocol> protocol;
  std::string output;
  unsigned port = 0;
#ifdef _WIN32
  bool sockets = false;
#endif
  ~Impl() {
    disconnect();
    close();
#ifdef _WIN32
    if (sockets)
      WSACleanup();
#endif
  }
  void disconnect() {
    protocol.reset();
    if (client != gdb::invalid_socket)
      gdb::close_socket(std::exchange(client, gdb::invalid_socket));
    output.clear();
  }
  void close() {
    disconnect();
    if (listener != gdb::invalid_socket)
      gdb::close_socket(std::exchange(listener, gdb::invalid_socket));
    port = 0;
  }
};

GdbServer::GdbServer() : impl_(std::make_unique<Impl>()) {}
GdbServer::~GdbServer() = default;

void GdbServer::listen(unsigned port) {
  if (port > 65535)
    throw std::runtime_error("Choose a debugger port from 1 to 65535.");
  if (impl_->listener != gdb::invalid_socket && port && port == impl_->port)
    return;
#ifdef _WIN32
  if (!impl_->sockets) {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data))
      throw std::runtime_error("Could not initialize the debugger network connection.");
    impl_->sockets = true;
  }
#endif
  const auto listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listener == gdb::invalid_socket)
    throw std::runtime_error("Could not create the debugger listener.");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<std::uint16_t>(port));
#ifdef _WIN32
  const int exclusive = 1;
  setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive),
             sizeof(exclusive));
#endif
  gdb::SocketLength length = sizeof(address);
  if (bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) ||
      ::listen(listener, 1) || !gdb::nonblocking(listener) ||
      getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length)) {
    gdb::close_socket(listener);
    throw std::runtime_error(
        "Could not listen on the selected debugger port. Choose another port.");
  }
  impl_->close();
  impl_->listener = listener;
  impl_->port = ntohs(address.sin_port);
}

void GdbServer::close() {
  impl_->close();
}

void GdbServer::disconnect() {
  impl_->disconnect();
}

void GdbServer::target(n64::Debugger *debugger, std::function<void()> memory_fence) {
  impl_->disconnect();
  impl_->debugger = debugger;
  impl_->memory_fence = std::move(memory_fence);
}

unsigned GdbServer::port() const {
  return impl_->port;
}

bool GdbServer::connected() const {
  return bool(impl_->protocol);
}

void GdbServer::poll() {
  if (impl_->listener == gdb::invalid_socket)
    return;
  if (impl_->client == gdb::invalid_socket) {
    const auto client = accept(impl_->listener, nullptr, nullptr);
    if (client == gdb::invalid_socket)
      return;
    if (!impl_->debugger || !gdb::nonblocking(client)) {
      gdb::close_socket(client);
      return;
    }
    const int enabled = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&enabled),
               sizeof(enabled));
    impl_->client = client;
    try {
      impl_->protocol = std::make_unique<GdbProtocol>(*impl_->debugger, impl_->memory_fence);
    } catch (...) {
      impl_->disconnect();
      throw;
    }
  }
  try {
    for (unsigned budget = 0; budget < GdbProtocol::maximum_packet; budget += 4096) {
      std::array<char, 4096> input{};
      const auto count = recv(impl_->client, input.data(), static_cast<int>(input.size()), 0);
      if (!count || (count < 0 && !gdb::pending())) {
        impl_->disconnect();
        return;
      }
      if (count < 0)
        break;
      impl_->protocol->receive(std::string_view(input.data(), static_cast<std::size_t>(count)));
      if (impl_->protocol->closing())
        break;
    }
    impl_->protocol->notify_stop();
    auto output = impl_->protocol->take_output();
    if (output.size() > 131072 - impl_->output.size())
      throw std::runtime_error("The debugger is not reading its replies.");
    impl_->output += output;
    if (!impl_->output.empty()) {
#ifdef _WIN32
      constexpr int flags = 0;
#else
      constexpr int flags = MSG_NOSIGNAL;
#endif
      const auto count =
          send(impl_->client, impl_->output.data(), static_cast<int>(impl_->output.size()), flags);
      if (count < 0 && !gdb::pending()) {
        impl_->disconnect();
        return;
      }
      if (count > 0)
        impl_->output.erase(0, static_cast<std::size_t>(count));
    }
    if (impl_->output.empty() && impl_->protocol->closing())
      impl_->disconnect();
  } catch (...) {
    impl_->disconnect();
    throw;
  }
}

} // namespace cupid::desktop
