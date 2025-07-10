#pragma once

#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>

#include <string>
#include <unordered_map>

#include "Buffer.h"
#include "ServerBase.h"

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
