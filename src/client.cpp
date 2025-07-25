/* g++ -Wall -Wextra -O2 -g client.cpp -o client.exe */

#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

enum class Status : uint32_t { Valid, Invalid, Error, Close };

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

Status send_message(int client_fd, std::vector<std::string>& str_list) {
  if (str_list[0] == "close") {
    return Status::Close;
  }

  if (str_list[0] != "get" && str_list[0] != "set" && str_list[0] != "del") {
    return Status::Invalid;
  } else if (str_list[0] == "get" && str_list.size() != 2U) {
    return Status::Invalid;
  } else if (str_list[0] == "set" && str_list.size() != 3U) {
    return Status::Invalid;
  } else if (str_list[0] == "del" && str_list.size() != 2U) {
    return Status::Invalid;
  }

  uint32_t total_len = 4U;
  uint32_t n_strs = str_list.size();
  for (auto const& s : str_list) {
    total_len += 4 + s.size();
  }
  std::vector<uint8_t> write_buffer;

  // write msg_len
  write_buffer.insert(write_buffer.end(), (const uint8_t*)&total_len,
                      (const uint8_t*)&total_len + 4);
  // write n_strs
  write_buffer.insert(write_buffer.end(), (const uint8_t*)&n_strs,
                      (const uint8_t*)&n_strs + 4);

  for (auto const& s : str_list) {
    size_t msg_len = s.size();
    write_buffer.insert(write_buffer.end(), (const uint8_t*)&msg_len,
                        (const uint8_t*)&msg_len + 4);
    write_buffer.insert(write_buffer.end(),
                        reinterpret_cast<const uint8_t*>(s.data()),
                        reinterpret_cast<const uint8_t*>(s.data()) + msg_len);
  }
  uint8_t err =
      write_all(client_fd, reinterpret_cast<const char*>(write_buffer.data()),
                write_buffer.size());

  if (err) {
    return Status::Error;
  }

  return Status::Valid;
}

void get_response(int client_fd) {
  // get response length
  char buffer[1024] = {};
  uint8_t err = read_all(client_fd, buffer, 4);
  uint32_t response_len = 0;
  memcpy(&response_len, buffer, 4);

  // read the response now that we know the length
  err = read_all(client_fd, buffer + 4, response_len);
  if (err) {
    std::cerr << "Error retriving response from server\n";
    return;
  }

  Status resp_status;
  memcpy(&resp_status, buffer + 4U, 4U);
  std::string server_resp;
  server_resp.assign(buffer + 8, response_len - 4);

  if (resp_status == Status::Valid) {
    std::cout << "Command successfully processed\n";
  } else if (resp_status == Status::Invalid) {
    std::cout << "Key not found\n";
  }

  if (server_resp.size() > 0) {
    std::cout << "Server response: " << server_resp << "\n";
  }
}

std::vector<std::string> parse_user_input(const std::string& input_str) {
  std::vector<std::string> args;
  std::istringstream iss(input_str);
  std::string token;

  while (iss >> token) {
    args.push_back(token);
  }

  return args;
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

  std::string user_input;

  // loop until user requests a close
  while (1) {
    std::cout << "> ";
    std::getline(std::cin, user_input);

    if (user_input.empty()) continue;

    std::vector<std::string> args = parse_user_input(user_input);
    Status status = send_message(client_fd, args);
    if (status == Status::Invalid) {
      std::cout << "Invalid input\n";
      continue;
    } else if (status == Status::Error) {
      std::cerr << "Error sending message to server\n";
      continue;
    } else if (status == Status::Close) {
      std::cout << "User requested close\n";
      break;
    }

    get_response(client_fd);
  }

  close(client_fd);
  std::cout << "Closed client socket\n";
  return 0;
}