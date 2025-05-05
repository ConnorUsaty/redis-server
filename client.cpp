/* g++ -Wall -Wextra -O2 -g client.cpp -o client.exe */

#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <iostream>

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

uint8_t write_all(int client_fd, char* buffer, int n_bytes) {
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
  char test_message[] = "ping";
  uint8_t err = write_all(client_fd, test_message, strlen(test_message));
  if (err) {
    std::cerr << "Error sending message to server\n";
    return 1;
  }
  std::cout << "Sent '" << test_message << "' to server\n";

  // read response from server
  char buffer[5] = {};
  err = read_all(client_fd, buffer, sizeof(buffer) - 1);
  if (err) {
    std::cerr << "Error retriving response from server\n";
    return 1;
  }
  std::cout << "Server says: " << buffer << "\n";

  // close client socket
  close(client_fd);
  std::cout << "Closed client socket\n";

  return 0;
}