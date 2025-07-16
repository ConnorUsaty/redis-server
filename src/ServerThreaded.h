#pragma once

#include <arpa/inet.h>
#include <unistd.h>

#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "Buffer.h"
#include "ServerBase.h"

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
        server_resp.status = Status::Invalid;
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
      server_resp.status = Status::Invalid;
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
