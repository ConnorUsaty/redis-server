/* g++ -Wall -Wextra -O2 -g server.cpp -o server.exe */

#include <fcntl.h>
#include <netinet/ip.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "buffer.h"
#include "server.h"

static std::map<std::string, std::string> server_data;

/* Need to parse client_msg which follows:
 * n_strs | len1 | str1 | len2 | str2 | ...
 * " | " is there for readability and is not actually in the msg */
int parse_msg(Buffer& read_buf, std::vector<std::string>& str_list) {
  size_t rel_buf_idx = 4U;  // offset from msg_len

  uint32_t n_strs = 0;
  memcpy(&n_strs, read_buf.data() + rel_buf_idx, 4U);
  if (n_strs == 0) return -1;
  rel_buf_idx += 4;

  while (str_list.size() < n_strs) {
    uint32_t str_len = 0;
    memcpy(&str_len, read_buf.data() + rel_buf_idx, 4U);
    if (str_len == 0) return -1;
    rel_buf_idx += 4;

    std::string str;
    str.assign((const char*)(read_buf.data() + rel_buf_idx),
               static_cast<size_t>(str_len));
    rel_buf_idx += str_len;
    str_list.push_back(str);
  }

  return 0;
}

void respond_to_client(std::vector<std::string>& client_cmd,
                       Buffer& write_buf) {
  Response server_resp;

  if (client_cmd[0] == "get") {
    auto it = server_data.find(client_cmd[1]);
    if (it == server_data.end()) {
      server_resp.status = 1;
    } else {
      server_resp.data.append(reinterpret_cast<uint8_t*>(&it->second[0]),
                              static_cast<uint32_t>(it->second.size()));
    }
  } else if (client_cmd[0] == "set") {
    server_data[client_cmd[1]] = client_cmd[2];
  } else if (client_cmd[0] == "del") {
    server_data.erase(client_cmd[1]);
  } else {
    server_resp.status = 1;
  }

  uint32_t resp_len = 4 + static_cast<uint32_t>(server_resp.data.size());
  write_buf.append(reinterpret_cast<uint8_t*>(&resp_len), 4U);
  write_buf.append(reinterpret_cast<uint8_t*>(&server_resp.status), 4U);
  if (server_resp.data.size() > 0) {
    write_buf.append(server_resp.data.data(),
                     static_cast<uint32_t>(server_resp.data.size()));
  }
}

bool parse_buffer(Conn* conn) {
  if (conn->read_buf.size() < 4) return false;

  // first 4 bytes of msg stores total size of msg in bytes
  // this size includes all lens and all strs in message
  uint32_t msg_len = 0;
  memcpy(&msg_len, static_cast<void*>(conn->read_buf.data()), 4);
  if (4 + msg_len > conn->read_buf.size()) {
    return false;
  }

  std::vector<std::string> client_cmd;
  if (parse_msg(conn->read_buf, client_cmd) < 0) {
    conn->want_close = true;
    return false;
  }

  respond_to_client(client_cmd, conn->write_buf);
  conn->read_buf.consume(msg_len + 4);

  return true;
}

void handle_write(Conn* conn) {
  /* Non-blocking write to buffer */
  ssize_t rv = send(conn->fd, static_cast<void*>(conn->write_buf.data()),
                    conn->write_buf.size(), 0);
  if (rv < 0) {
    if (errno != EAGAIN) conn->want_close = true;
    return;
  }

  if (rv == static_cast<ssize_t>(conn->write_buf.size())) {
    conn->want_write = false;
    conn->want_read = true;
    conn->write_buf.clear();
  } else {
    conn->write_buf.consume(rv);
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

  conn->read_buf.append(buf, rv);
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