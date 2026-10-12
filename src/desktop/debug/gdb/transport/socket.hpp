#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cupid::desktop::gdb {

#ifdef _WIN32
using Socket = SOCKET;
using SocketLength = int;
constexpr Socket invalid_socket = INVALID_SOCKET;
inline void close_socket(Socket value) {
  closesocket(value);
}
inline bool pending() {
  return WSAGetLastError() == WSAEWOULDBLOCK;
}
inline bool nonblocking(Socket value) {
  u_long enabled = 1;
  return ioctlsocket(value, FIONBIO, &enabled) == 0;
}
#else
using Socket = int;
using SocketLength = socklen_t;
constexpr Socket invalid_socket = -1;
inline void close_socket(Socket value) {
  ::close(value);
}
inline bool pending() {
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}
inline bool nonblocking(Socket value) {
  const auto flags = fcntl(value, F_GETFL, 0);
  return flags >= 0 && fcntl(value, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

} // namespace cupid::desktop::gdb
