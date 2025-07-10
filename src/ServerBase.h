#pragma once

#include <fcntl.h>
#include <sys/socket.h>

#include <vector>

#include "buffer.h"

struct Response {
  uint32_t status = 0;
  Buffer data{64};
};

struct Conn {
  /* Struct that contains all relevant data for an open connection */

  int fd = -1;  // -1 means connection closed

  bool want_read = false;
  bool want_write = false;
  bool want_close = false;

  Buffer write_buf{256};
  Buffer read_buf{256};

  Conn() = default;
};

/* ServerBase is a base class meant to be inherited and used to implement the
 * different concurrency architectures (multi-threaded, event-based) */
class ServerBase {
 protected:
  uint16_t port_;
  int64_t server_fd_;

  /* Need to parse client_msg which follows:
   * n_strs | len1 | str1 | len2 | str2 | ...
   * " | " is there for readability and is not actually in the msg */
  int parse_msg(Buffer& read_buf, std::vector<std::string>& client_cmd) {
    size_t rel_buf_idx = 4U;  // offset from msg_len

    uint32_t n_strs = 0;
    memcpy(&n_strs, read_buf.data() + rel_buf_idx, 4U);
    if (n_strs == 0) return -1;
    rel_buf_idx += 4;

    while (client_cmd.size() < n_strs) {
      uint32_t str_len = 0;
      memcpy(&str_len, read_buf.data() + rel_buf_idx, 4U);
      if (str_len == 0) return -1;
      rel_buf_idx += 4;

      std::string str;
      str.assign((const char*)(read_buf.data() + rel_buf_idx),
                 static_cast<size_t>(str_len));
      rel_buf_idx += str_len;
      client_cmd.push_back(str);
    }

    return 0;
  }

  void fd_set_nb(int fd) {
    /* Sets fd to non-blocking mode */
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  }

  int64_t setup_socket() {
    /* Socket setup is the same for all concurrency architectures */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // set options for socket, most important is SO_REUSEADDR, without setting
    // socket cannot bind to same IP:port after restart
    int val = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // set socket addresses
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(0);

    // bind socket to the port
    if (bind(server_fd, (const struct sockaddr*)&addr, sizeof(addr)) != 0) {
      std::cerr << "Failed to bind to port\n";
      return -1;
    }

    // set port to be listening (server), rather then connecting (client)
    if (listen(server_fd, SOMAXCONN) != 0) {
      std::cerr << "Failed to listen\n";
      return -1;
    }

    fd_set_nb(server_fd);
    return server_fd;
  }

 public:
  ServerBase(int port) : port_(port) {
    server_fd_ = setup_socket();
    if (server_fd_ < 0) {
      throw std::runtime_error("Failed to open server socket\n");
    }
  }
};
