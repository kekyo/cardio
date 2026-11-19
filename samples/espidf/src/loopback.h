#pragma once

#include "cardio.h"
#include "lwip/sockets.h"
#include <fcntl.h>
#include <unistd.h>

// Own one socket across suspension and release it on every return path.
struct sample_socket {
  int fd;
  explicit sample_socket(int fd) : fd(fd) {}
  ~sample_socket() { if (fd >= 0) ::close(fd); }
  sample_socket(const sample_socket&) = delete;
  sample_socket& operator=(const sample_socket&) = delete;
  bool nonblocking() const { return fd >= 0 && fcntl(fd, F_SETFL, O_NONBLOCK) == 0; }
};

static inline cardio::promise<bool> loopback_exchange() {
  using cardio::fd_event;
  sample_socket listener(socket(AF_INET, SOCK_STREAM, 0));
  sample_socket client(socket(AF_INET, SOCK_STREAM, 0));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  auto* endpoint = reinterpret_cast<sockaddr*>(&address);
  socklen_t length = sizeof(address);
  if (!listener.nonblocking() || !client.nonblocking() ||
      bind(listener.fd, endpoint, length) || listen(listener.fd, 1) ||
      getsockname(listener.fd, endpoint, &length)) {
    co_return false;
  }
  if (connect(client.fd, endpoint, length) < 0 && errno != EINPROGRESS) {
    co_return false;
  }
  co_await cardio::from_fd(client.fd, fd_event::write);
  int error = 0;
  socklen_t error_size = sizeof(error);
  if (getsockopt(client.fd, SOL_SOCKET, SO_ERROR, &error, &error_size) || error) {
    co_return false;
  }
  co_await cardio::from_fd(listener.fd, fd_event::read);
  sample_socket server(accept(listener.fd, nullptr, nullptr));
  if (!server.nonblocking() || send(client.fd, "C", 1, 0) != 1 ||
      shutdown(client.fd, SHUT_WR)) {
    co_return false;
  }
  char data = 0;
  co_await cardio::from_fd(server.fd, fd_event::read);
  if (recv(server.fd, &data, 1, 0) != 1 || data != 'C') {
    co_return false;
  }
  co_await cardio::from_fd(server.fd, fd_event::read);
  co_return recv(server.fd, &data, 1, 0) == 0;
}
