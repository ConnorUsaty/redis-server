/* g++ -Wall -Wextra -O2 -g server.cpp -o server.exe */

#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>

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

void handle_request(int client_fd) {
  std::cout << "Connected to client: " << client_fd << "\n";

  char buffer[64] = {};
  uint8_t err = read_all(client_fd, buffer, 4);
  if (err) {
    std::cerr << "Error reading from client\n";
    return;
  }
  std::cout << "Client says: '" << buffer << "'\n";

  char response[] = "pong";
  err = write_all(client_fd, response, strlen(response));
  if (err) {
    std::cerr << "Error writing to client\n";
    return;
  }
  std::cout << "Sent '" << response << "' to client\n";
  close(client_fd);  // close connection to client
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

  while (1) {
    // accept incoming connections and get connection fd
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);

    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
    if (client_fd < 0) {
      continue;
    }

    std::thread t(handle_request, client_fd);
    t.detach();
  }

  // close the listening server socket
  close(server_fd);

  return 0;
}