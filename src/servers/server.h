#pragma once

#include <iostream>
#include <string>
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

// function forward declarations
void respond_to_client(Buffer& write_buf, const uint8_t* client_msg,
                       uint32_t msg_len);
bool parse_buffer(Conn* conn);
void handle_write(Conn* conn);
void handle_read(Conn* conn);
void fd_set_nb(int fd);
Conn* handle_accept(int server_fd);
int setup_socket(const int PORT);

// TODO: Move every class into its own .cpp and .h file
// TODO: Move all functional testing into a tests/ dir and just have the classes
// in this dir

/* ServerBase is a base class meant to be inherited and used to implement the 3
 * concurrency architectures (multi-processed, multi-threaded, event-based) */
class ServerBase {
 protected:
  int32_t port_;
  int64_t server_fd_;

  int64_t setup_socket() {
    /* Socket setup is the same for all 3 concurrency architectures */
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
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(0);

    // bind socket to the port
    if (bind(server_fd, (const struct sockaddr*)&addr, sizeof(addr)) != 0) {
      std::cerr << "Failed to bind to port\n";
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

 public:
  ServerBase(int port) : port_(port) {
    server_fd_ = setup_socket();
    if (server_fd_ < 0) {
      throw std::runtime_error("Failed to open server socket\n");
    }
    std::cout << "Server socket successfully setup!\n";
  }

  // functions required for all 3 architectures but with unique implementations
  virtual void handle_accept() = 0;
  virtual void handle_read() = 0;
  virtual void handle_write() = 0;
  virtual void parse_buffer() = 0;
  virtual void respond_to_client() = 0;
};
