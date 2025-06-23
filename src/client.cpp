/* g++ -Wall -Wextra -O2 -g client.cpp -o client.exe */

#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

uint8_t read_all(int client_fd, char* buffer, int n_bytes) {
  /* Ensures that all requested n_bytes are read from socket into buffer.
   * recv/read are not guarenteed to return all n_bytes. */

  while (n_bytes) {
    ssize_t rv = recv(client_fd, buffer, n_bytes, 0);
    if (rv <= 0) {
      return 1;  // error or unexpected EOF
    }
    assert((int)rv <= n_bytes);
    n_bytes -= rv;
    buffer += rv;  // move pointer to next avail location
  }
  return 0;
}

uint8_t write_all(int client_fd, const char* buffer, int n_bytes) {
  /* Ensures that all requested n_bytes are written from buffer into socket.
   * send/write are not guarenteed to return all n_bytes. */
  while (n_bytes) {
    ssize_t rv = send(client_fd, buffer, n_bytes, 0);
    if (rv <= 0) {
      return 1;  // error
    }
    assert((int)rv <= n_bytes);
    n_bytes -= rv;
    buffer += rv;  // move pointer to next unwritten location
  }
  return 0;
}

void send_message(int client_fd, std::string s) {
  std::vector<uint8_t> write_buffer;
  size_t msg_len = s.size();
  write_buffer.insert(write_buffer.end(), (const uint8_t*)&msg_len,
                      (const uint8_t*)&msg_len + 4);
  write_buffer.insert(write_buffer.end(),
                      reinterpret_cast<const uint8_t*>(s.data()),
                      reinterpret_cast<const uint8_t*>(s.data()) + msg_len);
  uint8_t err =
      write_all(client_fd, reinterpret_cast<const char*>(write_buffer.data()),
                write_buffer.size());

  if (err) {
    std::cerr << "Error sending message to server\n";
    return;
  }
  std::cout << "Sent '" << s << "' to server\n";
}

void get_response(int client_fd) {
  // get response length
  char buffer[1024] = {};
  uint8_t err = read_all(client_fd, buffer, 4);
  uint32_t response_len = 0;
  memcpy(&response_len, buffer, 4);

  // read the response now that we know the length
  err = read_all(client_fd, buffer + 4, response_len);
  const char* server_response = buffer + 4;
  if (err) {
    std::cerr << "Error retriving response from server\n";
    return;
  }
  std::cout << "Server says: " << server_response << "\n";
}

int main() {
  // get socket (allocate socket and get client socket fd)
  int client_fd = socket(AF_INET, SOCK_STREAM, 0);

  // set socket adresses
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);      // port
  addr.sin_addr.s_addr = htonl(0);  // wildcard IP 0.0.0.0

  // set as connector socket (client) and initiate connection with server by
  // putting in same same address (port) as server
  if (connect(client_fd, (const struct sockaddr*)&addr, sizeof(addr)) != 0) {
    std::cerr << "Error connecting to server\n";
    return 1;
  }

  // send test message to server
  std::string large_str(1 << 8, '_');
  std::vector<std::string> message_queue = {"ping", large_str, "ping",
                                            large_str, "random"};
  for (const auto& s : message_queue) {
    send_message(client_fd, s);
  }
  for (int i = 0; i < static_cast<int>(message_queue.size()); ++i) {
    get_response(client_fd);
  }

  close(client_fd);
  std::cout << "Closed client socket\n";
  return 0;
}