#include <gtest/gtest.h>

#include "Buffer.h"
#include "ServerEventLoop.h"
#include "ServerThreaded.h"

// helper to create a client connection
int create_client_connection(uint16_t port) {
  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd < 0) return -1;

  struct sockaddr_in server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) <
      0) {
    close(client_fd);
    return -1;
  }

  return client_fd;
}

// helper to build protocol messages
std::vector<uint8_t> build_message(const std::vector<std::string>& parts) {
  std::vector<uint8_t> msg;

  // space for total length (will fill later)
  msg.resize(4);

  // add number of strings
  uint32_t n_strs = parts.size();
  msg.insert(msg.end(), reinterpret_cast<uint8_t*>(&n_strs),
             reinterpret_cast<uint8_t*>(&n_strs) + 4);

  // add each string with its length
  for (const auto& part : parts) {
    uint32_t len = part.size();
    msg.insert(msg.end(), reinterpret_cast<uint8_t*>(&len),
               reinterpret_cast<uint8_t*>(&len) + 4);
    msg.insert(msg.end(), part.begin(), part.end());
  }

  // fill in total length (excluding the 4 bytes for length itself)
  uint32_t total_len = msg.size() - 4;
  memcpy(msg.data(), &total_len, 4);

  return msg;
}

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

// helper that parses response from the server
void parse_response(int client_fd, uint32_t& res_len, uint32_t& res_status,
                    std::string& res_msg) {
  // get response length
  char buffer[1024] = {};
  uint8_t err = read_all(client_fd, buffer, 4);
  memcpy(&res_len, buffer, 4);

  // read the response now that we know the length
  err = read_all(client_fd, buffer + 4, res_len);
  ASSERT_EQ(err, 0);

  // place response into its respective variables
  memcpy(&res_status, buffer + 4U, 4U);
  res_msg.assign(buffer + 8, res_len - 4);
}

class ServerTestBase : public ::testing::Test {
 protected:
  static constexpr uint16_t BASE_PORT = 9999;
  static std::atomic<uint16_t> port_counter;

  uint16_t get_next_port() { return BASE_PORT + port_counter.fetch_add(1); }

  void SetUp() override {}
  void TearDown() override {}
};

std::atomic<uint16_t> ServerTestBase::port_counter{0};

class ServerEventLoopTest : public ServerTestBase {};

TEST_F(ServerEventLoopTest, BasicAllCmdTest) {
  uint16_t port = get_next_port();
  ServerEventLoop server(port);

  std::thread server_thread([&server]() { server.run_server(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  int client_fd = create_client_connection(port);
  ASSERT_GT(client_fd, 0);

  // build up a large queue and send all messages at once
  std::vector<std::vector<uint8_t>> message_queue;
  std::string large_msg(1 << 8, '_');
  std::string large_val(1 << 6, '*');
  message_queue.push_back(build_message({"get", "notakey"}));
  message_queue.push_back(build_message({"set", "realkey", "realval"}));
  message_queue.push_back(build_message({"get", "realkey"}));
  message_queue.push_back(build_message({"del", "realkey"}));
  message_queue.push_back(build_message({"set", large_msg, large_val}));
  message_queue.push_back(build_message({"get", large_msg}));
  message_queue.push_back(build_message({"get", "realkey"}));
  for (const auto& msg : message_queue) {
    ASSERT_EQ(send(client_fd, msg.data(), msg.size(), 0),
              static_cast<ssize_t>(msg.size()));
  }

  // parse all messages and ensure we get the expected results
  // res_len does NOT include the 4 bytes for res_len just the 4 +
  // res_msg.size()
  std::vector<uint32_t> expected_res_len = {
      4U, 4U, 11U, 4U, 4U, (1U << 6U) + 4U, 4U};
  std::vector<uint32_t> expected_res_status = {1U, 0U, 0U, 0U, 0U, 0U, 1U};
  std::vector<std::string> expected_res_msg = {"", "",        "realval", "",
                                               "", large_val, ""};
  for (size_t i = 0; i < message_queue.size(); ++i) {
    uint32_t res_len{};
    uint32_t res_status{};
    std::string res_msg{};
    parse_response(client_fd, res_len, res_status, res_msg);

    EXPECT_EQ(expected_res_len[i], res_len);
    EXPECT_EQ(expected_res_status[i], res_status);
    EXPECT_EQ(expected_res_msg[i], res_msg);
  }

  // clean up threads / sockets
  close(client_fd);
  pthread_cancel(server_thread.native_handle());
  server_thread.detach();
}

TEST_F(ServerEventLoopTest, MultipleConnectionsTest) {
  uint16_t port = get_next_port();
  ServerEventLoop server(port);

  std::thread server_thread([&server]() { server.run_server(); });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const int NUM_CLIENTS = 10;
  std::vector<int> clients;

  // create multiple connections
  for (size_t i = 0; i < NUM_CLIENTS; ++i) {
    int fd = create_client_connection(port);
    ASSERT_GT(fd, 0);
    clients.push_back(fd);
  }

  for (size_t i = 0; i < NUM_CLIENTS; ++i) {
    std::string key = "key" + std::to_string(i);
    std::string value = "value" + std::to_string(i);
    auto msg = build_message({"set", key, value});

    ASSERT_EQ(send(clients[i], msg.data(), msg.size(), 0),
              static_cast<ssize_t>(msg.size()));
  }

  for (size_t i = 0; i < NUM_CLIENTS; ++i) {
    uint8_t response[1024];
    ssize_t n = recv(clients[i], response, sizeof(response), 0);
    ASSERT_GT(n, 0);
  }

  // verify all keys are accessible
  for (size_t i = 0; i < NUM_CLIENTS; ++i) {
    std::string key = "key" + std::to_string(i);
    auto msg = build_message({"get", key});

    ASSERT_EQ(send(clients[0], msg.data(), msg.size(), 0),
              static_cast<ssize_t>(msg.size()));

    uint8_t response[1024];
    ssize_t n = recv(clients[0], response, sizeof(response), 0);
    ASSERT_GT(n, 8);

    uint32_t status;
    memcpy(&status, response + 4, 4);
    EXPECT_EQ(status, 0);
  }

  for (int fd : clients) {
    close(fd);
  }

  pthread_cancel(server_thread.native_handle());
  server_thread.detach();
}

class ServerThreadedTest : public ServerTestBase {};

TEST_F(ServerThreadedTest, ConcurrentAccessTest) {
  uint16_t port = get_next_port();
  ServerThreaded server(port);

  std::thread server_thread([&server]() { server.run_server(); });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  const int NUM_THREADS = 20;
  const int OPS_PER_THREAD = 100;
  std::vector<std::thread> client_threads;
  std::atomic<int> success_count{0};

  for (int t = 0; t < NUM_THREADS; t++) {
    client_threads.emplace_back([&, t]() {
      int client_fd = create_client_connection(port);
      if (client_fd < 0) return;

      for (int op = 0; op < OPS_PER_THREAD; op++) {
        std::string key = "key" + std::to_string(t) + "_" + std::to_string(op);
        std::string value =
            "value" + std::to_string(t) + "_" + std::to_string(op);

        // SET operation
        auto set_msg = build_message({"set", key, value});
        if (send(client_fd, set_msg.data(), set_msg.size(), 0) !=
            static_cast<ssize_t>(set_msg.size())) {
          break;
        }

        uint8_t response[1024];
        ssize_t n = recv(client_fd, response, sizeof(response), 0);
        if (n <= 0) break;

        // GET operation to verify
        auto get_msg = build_message({"get", key});
        if (send(client_fd, get_msg.data(), get_msg.size(), 0) !=
            static_cast<ssize_t>(get_msg.size())) {
          break;
        }

        n = recv(client_fd, response, sizeof(response), 0);
        if (n > 8) {
          uint32_t msg_len, status;
          memcpy(&msg_len, response, 4);
          memcpy(&status, response + 4, 4);
          if (status == 0) {
            std::string retrieved(reinterpret_cast<char*>(response + 8),
                                  msg_len - 4);
            if (retrieved == value) {
              success_count++;
            }
          }
        }
      }

      close(client_fd);
    });
  }

  for (auto& t : client_threads) {
    t.join();
  }

  EXPECT_EQ(success_count, NUM_THREADS * OPS_PER_THREAD);

  pthread_cancel(server_thread.native_handle());
  server_thread.detach();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
