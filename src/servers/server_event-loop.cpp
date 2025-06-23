/* g++ -Wall -Wextra -O2 -g server.cpp -o server.exe */

#include <fcntl.h>
#include <netinet/ip.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "server.h"

void respond_to_client(std::vector<uint8_t>& write_buf,
                       const uint8_t* client_msg, uint32_t msg_len) {
  /* Respond to client based on their message */
  const char* response;
  if (strncmp(reinterpret_cast<const char*>(client_msg), "ping", msg_len) ==
      0) {
    response = "pong";
  } else {
    response = "unknown request";
  }

  size_t res_len = strlen(response);
  write_buf.insert(write_buf.end(), (const uint8_t*)&res_len,
                   (const uint8_t*)&res_len + 4);
  write_buf.insert(write_buf.end(), reinterpret_cast<const uint8_t*>(response),
                   reinterpret_cast<const uint8_t*>(response) + res_len);
}

bool parse_buffer(Conn* conn) {
  if (conn->read_buf.size() < 4) return false;

  // first 4 bytes in read_buf store size of msg in bytes
  uint32_t msg_len = 0;
  memcpy(&msg_len, conn->read_buf.data(), 4);
  if (4 + msg_len > conn->read_buf.size()) {
    return false;
  }

  const uint8_t* client_msg = &conn->read_buf[4];
  std::cout << "Client says: '" << client_msg << "'\n";
  respond_to_client(conn->write_buf, client_msg, msg_len);

  conn->read_buf.erase(conn->read_buf.begin(),
                       conn->read_buf.begin() + 4 + msg_len);
  return true;
}

void handle_write(Conn* conn) {
  /* Non-blocking write to buffer */
  ssize_t rv =
      send(conn->fd, conn->write_buf.data(), conn->write_buf.size(), 0);
  if (rv < 0) {
    if (errno != EAGAIN) conn->want_close = true;
    return;
  }

  if (rv == static_cast<ssize_t>(conn->write_buf.size())) {
    conn->want_write = false;
    conn->want_read = true;
    conn->write_buf.clear();
  } else {
    conn->write_buf.erase(conn->write_buf.begin(),
                          conn->write_buf.begin() + rv);
  }
}

void handle_read(Conn* conn) {
  /* Non-blocking read from buffer */
  uint8_t buf[64 * 1024];
  auto rv = recv(conn->fd, buf, sizeof(buf) - 1, 0);
  if (rv < 0) {
    conn->want_close = true;
    return;
  }
  if (rv == 0 && conn->read_buf.size() == 0) {
    // client closed connection
    conn->want_close = true;
    return;
  }

  // add to current accumulated reads from this conn
  auto i = conn->read_buf.size();
  conn->read_buf.resize(i + rv);
  memcpy(&conn->read_buf[i], buf, rv);

  while (parse_buffer(conn)) {
  };

  if (conn->write_buf.size() > 0) {
    conn->want_read = false;
    conn->want_write = true;
    handle_write(conn);
  }
}

void fd_set_nb(int fd) {
  /* Sets fd to non-blocking mode */
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

Conn* handle_accept(int server_fd) {
  /* Accept the first connection request in queue of pending connections to
   * server */
  struct sockaddr_in client_addr = {};
  socklen_t addrlen = sizeof(client_addr);

  int conn_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
  if (conn_fd < 0) {
    return nullptr;
  }

  fd_set_nb(conn_fd);
  Conn* conn = new Conn;
  conn->fd = conn_fd;
  conn->want_read = true;
  return conn;
}

int setup_socket(const int PORT) {
  // allocates a TCP socket and returns the fd
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  std::cout << "Created socket\n";

  // set options for socket, most important is SO_REUSEADDR, without setting
  // socket cannot bind to same IP:port after restart
  int val = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
  std::cout << "Set socket options\n";

  // set socket addresses
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT);
  addr.sin_addr.s_addr = htonl(0);  // wildcard IP 0.0.0.0

  // bind socket to the port
  if (bind(server_fd, (const struct sockaddr*)&addr, sizeof(addr)) != 0) {
    std::cerr << "Failed to bind to port 6379\n";
    return -1;
  }
  std::cout << "Bound socket to port\n";

  // set port to be listening (server), rather then connecting (client)
  if (listen(server_fd, SOMAXCONN) != 0) {
    std::cerr << "Failed to listen\n";
    return -1;
  }
  std::cout << "Set socket to listening\n";

  fd_set_nb(server_fd);
  std::cout << "Set socket to non-blocking\n";

  return server_fd;
}

int main() {
  const int PORT = 1234;
  int server_fd = setup_socket(PORT);
  if (server_fd == -1) {
    std::cerr << "Error setting up socket\n";
    return 1;
  }
  std::cout << "Socket successfully setup!\n";

  std::vector<Conn*> conn_list;  // key = fd, val = connection info
  std::vector<struct pollfd> poll_args;

  while (1) {
    poll_args.clear();
    struct pollfd pfd = {server_fd, POLLIN, 0};
    poll_args.push_back(pfd);

    for (Conn* conn : conn_list) {
      if (conn == nullptr) continue;

      struct pollfd pfd = {conn->fd, POLLERR, 0};
      if (conn->want_read) pfd.events |= POLLIN;
      if (conn->want_write) pfd.events |= POLLOUT;
      poll_args.push_back(pfd);
    }

    // blocks until ANY of the fd in poll_args become ready to perform I/O
    int rv = poll(poll_args.data(), static_cast<nfds_t>(poll_args.size()), -1);
    if (rv < 0 && errno == EINTR)
      continue;
    else if (rv < 0) {
      std::cerr << "Failed to connect";
      return 1;
    }

    // accept any new connections
    if (poll_args[0].revents) {
      if (Conn* conn = handle_accept(server_fd)) {
        if (conn_list.size() <= static_cast<size_t>(conn->fd)) {
          conn_list.resize(conn->fd + 1);
        }
        conn_list[conn->fd] = conn;
      }
    }

    // handle all open connections
    for (int i = 1; i < static_cast<int>(poll_args.size()); ++i) {
      uint32_t rdy = poll_args[i].revents;
      Conn* conn = conn_list[poll_args[i].fd];
      if (rdy & POLLIN) handle_read(conn);
      if (rdy & POLLOUT) handle_write(conn);

      if ((rdy & (POLLERR | POLLHUP)) || (conn->want_close)) {
        close(conn->fd);
        conn_list[conn->fd] = nullptr;
        delete conn;
      }
    }
  }

  close(server_fd);
  return 0;
}