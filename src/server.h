#pragma once

#include <fcntl.h>
#include <netinet/ip.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
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

class ServerEventLoop final : private ServerBase {
 private:
  std::unordered_map<std::string, std::string> server_data_;

  void respond_to_client(std::vector<std::string>& client_cmd,
                         Buffer& write_buf) {
    Response server_resp;

    if (client_cmd[0] == "get") {
      auto it = server_data_.find(client_cmd[1]);
      if (it == server_data_.end()) {
        server_resp.status = 1;
      } else {
        server_resp.data.append(reinterpret_cast<uint8_t*>(&it->second[0]),
                                static_cast<uint32_t>(it->second.size()));
      }
    } else if (client_cmd[0] == "set") {
      server_data_[client_cmd[1]] = client_cmd[2];
    } else if (client_cmd[0] == "del") {
      server_data_.erase(client_cmd[1]);
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

  Conn* handle_accept() {
    /* Accept the first connection request in queue of pending connections to
     * server */
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);

    int conn_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &addrlen);
    if (conn_fd < 0) {
      return nullptr;
    }

    fd_set_nb(conn_fd);
    Conn* conn = new Conn;
    conn->fd = conn_fd;
    conn->want_read = true;
    return conn;
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

 public:
  ServerEventLoop(int port) : ServerBase(port) {}

  int run_server() {
    std::vector<Conn*> conn_list;  // key = fd, val = connection info
    std::vector<struct pollfd> poll_args;

    while (1) {
      poll_args.clear();
      struct pollfd pfd = {static_cast<int>(server_fd_), POLLIN, 0};
      poll_args.push_back(pfd);

      for (Conn* conn : conn_list) {
        if (conn == nullptr) continue;

        struct pollfd pfd = {conn->fd, POLLERR, 0};
        if (conn->want_read) pfd.events |= POLLIN;
        if (conn->want_write) pfd.events |= POLLOUT;
        poll_args.push_back(pfd);
      }

      // blocks until ANY of the fd in poll_args become ready to perform I/O
      int rv =
          poll(poll_args.data(), static_cast<nfds_t>(poll_args.size()), -1);
      if (rv < 0 && errno == EINTR)
        continue;
      else if (rv < 0) {
        std::cerr << "Failed to connect";
        return 1;
      }

      // accept any new connections
      if (poll_args[0].revents) {
        if (Conn* conn = handle_accept()) {
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

    close(server_fd_);
    return 0;
  }
};

class ServerThreaded final : private ServerBase {
 private:
  std::unordered_map<std::string, std::string> server_data_;
  std::mutex mtx_;  // to protect server_data_ from race conditions

  void respond_to_client(std::vector<std::string>& client_cmd,
                         Buffer& write_buf) {
    Response server_resp;

    if (client_cmd[0] == "get") {
      std::scoped_lock lock_(mtx_);  // blocks until mutex free
      auto it = server_data_.find(client_cmd[1]);
      if (it == server_data_.end()) {
        server_resp.status = 1;
      } else {
        server_resp.data.append(reinterpret_cast<uint8_t*>(&it->second[0]),
                                static_cast<uint32_t>(it->second.size()));
      }
      // scoped_lock dtor called and mutex freed
    } else if (client_cmd[0] == "set") {
      std::scoped_lock lock_(mtx_);  // blocks until mutex free
      server_data_[client_cmd[1]] = client_cmd[2];
      // scoped_lock dtor called and mutex freed
    } else if (client_cmd[0] == "del") {
      std::scoped_lock lock_(mtx_);  // blocks until mutex free
      server_data_.erase(client_cmd[1]);
      // scoped_lock dtor called and mutex freed
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

  bool parse_buffer(Buffer& read_buf, Buffer& write_buf) {
    if (read_buf.size() < 4) return false;

    // first 4 bytes of msg stores total size of msg in bytes
    uint32_t msg_len = 0;
    memcpy(&msg_len, static_cast<void*>(read_buf.data()), 4);
    if (4 + msg_len > read_buf.size()) {
      return false;  // not enough data yet
    }

    std::vector<std::string> client_cmd;
    if (parse_msg(read_buf, client_cmd) < 0) {
      return false;
    }

    respond_to_client(client_cmd, write_buf);
    read_buf.consume(msg_len + 4);

    return true;
  }

  void handle_request(int client_fd) {
    Buffer read_buf{256};
    Buffer write_buf{256};
    uint8_t temp_buffer[64 * 1024];

    while (1) {
      ssize_t rv =
          recv(client_fd, temp_buffer, sizeof(temp_buffer), MSG_DONTWAIT);

      if (rv > 0) {
        // got data, append to read buffer
        read_buf.append(temp_buffer, rv);
      } else if (rv == 0) {
        // client closed connection
        break;
      } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        // real error
        std::cerr << "Error reading from client\n";
        break;
      }

      // process all complete messages in the buffer
      while (parse_buffer(read_buf, write_buf)) {
      }

      // send any pending responses
      if (write_buf.size() > 0) {
        ssize_t sent = 0;
        while (sent < static_cast<ssize_t>(write_buf.size())) {
          ssize_t rv = send(client_fd, write_buf.data() + sent,
                            write_buf.size() - sent, 0);
          if (rv <= 0) {
            std::cerr << "Error writing to client\n";
            close(client_fd);
            return;
          }
          sent += rv;
        }
        write_buf.clear();
      }

      // if no data was read and no data pending, wait a bit before trying again
      if (rv < 0 && read_buf.size() == 0) {
        usleep(1000);  // 1ms sleep to avoid busy waiting
      }
    }

    close(client_fd);
  }

 public:
  ServerThreaded(int port) : ServerBase(port) {}

  int run_server() {
    while (1) {
      // accept incoming connections and get connection fd
      struct sockaddr_in client_addr = {};
      socklen_t addrlen = sizeof(client_addr);

      int client_fd =
          accept(server_fd_, (struct sockaddr*)&client_addr, &addrlen);
      if (client_fd < 0) {
        continue;
      }

      std::thread t(&ServerThreaded::handle_request, this, client_fd);
      t.detach();
    }

    // close the listening server socket
    close(server_fd_);
    return 0;
  }
};