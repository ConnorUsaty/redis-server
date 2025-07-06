#include <arpa/inet.h>
#include <benchmark/benchmark.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include "server.h"

// global port counter to avoid conflicts
static std::atomic<uint16_t> g_port_counter{20000};

// helper to build protocol messages
std::vector<uint8_t> build_message(const std::vector<std::string>& parts) {
  std::vector<uint8_t> msg;
  msg.resize(4);

  uint32_t n_strs = parts.size();
  msg.insert(msg.end(), reinterpret_cast<uint8_t*>(&n_strs),
             reinterpret_cast<uint8_t*>(&n_strs) + 4);

  for (const auto& part : parts) {
    uint32_t len = part.size();
    msg.insert(msg.end(), reinterpret_cast<uint8_t*>(&len),
               reinterpret_cast<uint8_t*>(&len) + 4);
    msg.insert(msg.end(), part.begin(), part.end());
  }

  uint32_t total_len = msg.size() - 4;
  memcpy(msg.data(), &total_len, 4);

  return msg;
}

class BenchmarkClient {
 private:
  int fd_;
  std::vector<uint8_t> response_buffer_;

 public:
  BenchmarkClient(uint16_t port) : response_buffer_(4096) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
      throw std::runtime_error("Failed to create socket");
    }

    int flag = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    int bufsize = 64 * 1024;
    setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
    setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
      close(fd_);
      throw std::runtime_error("Failed to connect");
    }
  }

  ~BenchmarkClient() {
    if (fd_ >= 0) close(fd_);
  }

  void send_request(const std::vector<uint8_t>& msg) {
    if (send(fd_, msg.data(), msg.size(), 0) !=
        static_cast<ssize_t>(msg.size())) {
      throw std::runtime_error("Failed to send");
    }
  }

  void receive_response() {
    // read message length first
    ssize_t n = recv(fd_, response_buffer_.data(), 4, MSG_WAITALL);
    if (n != 4) throw std::runtime_error("Failed to receive length");

    uint32_t msg_len;
    memcpy(&msg_len, response_buffer_.data(), 4);

    // read rest of message
    n = recv(fd_, response_buffer_.data() + 4, msg_len, MSG_WAITALL);
    if (n != static_cast<ssize_t>(msg_len)) {
      throw std::runtime_error("Failed to receive data");
    }
  }

  void round_trip(const std::vector<uint8_t>& msg) {
    send_request(msg);
    receive_response();
  }
};

template <typename ServerType>
class ServerBenchmarkFixture : public benchmark::Fixture {
 protected:
  std::unique_ptr<ServerType> server_;
  std::thread server_thread_;
  uint16_t port_;
  std::atomic<bool> server_running_{false};

  void SetUp(const ::benchmark::State& state) override {
    port_ = g_port_counter.fetch_add(1);
    server_ = std::make_unique<ServerType>(port_);

    server_thread_ = std::thread([this]() {
      server_running_ = true;
      server_->run_server();
    });

    while (!server_running_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  void TearDown(const ::benchmark::State& state) override {
    // force kill server thread (not elegant but works for benchmarks)
    pthread_cancel(server_thread_.native_handle());
    server_thread_.detach();
    server_.reset();
  }
};

using EventLoopFixture = ServerBenchmarkFixture<ServerEventLoop>;
using ThreadedFixture = ServerBenchmarkFixture<ServerThreaded>;

// latency benchmark - single client round trip
BENCHMARK_DEFINE_F(EventLoopFixture, Latency_SingleClient)
(benchmark::State& state) {
  BenchmarkClient client(port_);
  auto msg = build_message({"get", "nonexistent_key"});

  size_t warmup_iterations = 1000U;
  for (size_t i = 0; i < warmup_iterations; ++i) {
    client.round_trip(msg);
  }

  for (auto _ : state) {
    auto start = std::chrono::high_resolution_clock::now();
    client.round_trip(msg);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    state.SetIterationTime(elapsed.count() / 1e9);
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("EventLoop");
}

BENCHMARK_DEFINE_F(ThreadedFixture, Latency_SingleClient)
(benchmark::State& state) {
  BenchmarkClient client(port_);
  auto msg = build_message({"get", "nonexistent_key"});

  size_t warmup_iterations = 1000U;
  for (size_t i = 0; i < warmup_iterations; ++i) {
    client.round_trip(msg);
  }

  for (auto _ : state) {
    auto start = std::chrono::high_resolution_clock::now();
    client.round_trip(msg);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    state.SetIterationTime(elapsed.count() / 1e9);
  }

  state.SetItemsProcessed(state.iterations());
  state.SetLabel("Threaded");
}

// throughput benchmark - multiple clients
BENCHMARK_DEFINE_F(EventLoopFixture, Throughput_MultiClient)
(benchmark::State& state) {
  const size_t num_clients = state.range(0);
  std::vector<std::unique_ptr<BenchmarkClient>> clients;

  for (size_t i = 0; i < num_clients; ++i) {
    clients.push_back(std::make_unique<BenchmarkClient>(port_));
  }

  auto get_msg = build_message({"get", "key1"});
  auto set_msg = build_message({"set", "key1", "value1"});

  // pre-populate some data
  clients[0]->round_trip(set_msg);

  std::atomic<int64_t> total_ops{0};

  for (auto _ : state) {
    std::vector<std::thread> client_threads;
    std::atomic<bool> stop{false};

    // launch client threads
    for (size_t i = 0; i < num_clients; ++i) {
      client_threads.emplace_back([&, i]() {
        int64_t ops = 0;
        while (!stop.load(std::memory_order_relaxed)) {
          try {
            clients[i]->round_trip(get_msg);
            ops++;
          } catch (...) {
            break;
          }
        }
        total_ops.fetch_add(ops);
      });
    }

    // run for 1 second
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;

    // wait for threads
    for (auto& t : client_threads) {
      t.join();
    }

    state.SetIterationTime(1.0);
  }

  state.SetItemsProcessed(total_ops.load());
  state.SetLabel("EventLoop");
}

BENCHMARK_DEFINE_F(ThreadedFixture, Throughput_MultiClient)
(benchmark::State& state) {
  const size_t num_clients = state.range(0);
  std::vector<std::unique_ptr<BenchmarkClient>> clients;

  for (size_t i = 0; i < num_clients; ++i) {
    clients.push_back(std::make_unique<BenchmarkClient>(port_));
  }

  auto get_msg = build_message({"get", "key1"});
  auto set_msg = build_message({"set", "key1", "value1"});

  // pre-populate some data
  clients[0]->round_trip(set_msg);

  std::atomic<int64_t> total_ops{0};

  for (auto _ : state) {
    std::vector<std::thread> client_threads;
    std::atomic<bool> stop{false};

    // launch client threads
    for (size_t i = 0; i < num_clients; ++i) {
      client_threads.emplace_back([&, i]() {
        int64_t ops = 0;
        while (!stop.load(std::memory_order_relaxed)) {
          try {
            clients[i]->round_trip(get_msg);
            ops++;
          } catch (...) {
            break;
          }
        }
        total_ops.fetch_add(ops);
      });
    }

    // run for 1 second
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;

    // wait for threads
    for (auto& t : client_threads) {
      t.join();
    }

    state.SetIterationTime(1.0);
  }

  state.SetItemsProcessed(total_ops.load());
  state.SetLabel("Threaded");
}

// mixed workload benchmark - mix of all cmds
BENCHMARK_DEFINE_F(EventLoopFixture, MixedWorkload)(benchmark::State& state) {
  const size_t num_clients = 4;
  std::vector<std::unique_ptr<BenchmarkClient>> clients;

  for (size_t i = 0; i < num_clients; ++i) {
    clients.push_back(std::make_unique<BenchmarkClient>(port_));
  }

  std::atomic<int64_t> total_ops{0};

  for (auto _ : state) {
    std::vector<std::thread> client_threads;
    std::atomic<bool> stop{false};

    for (size_t i = 0; i < num_clients; ++i) {
      client_threads.emplace_back([&, i]() {
        int64_t ops = 0;
        while (!stop.load(std::memory_order_relaxed)) {
          try {
            // 70% reads, 20% writes, 10% deletes
            int op_type = ops % 10;
            if (op_type < 7) {
              auto msg =
                  build_message({"get", "key" + std::to_string(ops % 100)});
              clients[i]->round_trip(msg);
            } else if (op_type < 9) {
              auto msg =
                  build_message({"set", "key" + std::to_string(ops % 100),
                                 "value" + std::to_string(ops)});
              clients[i]->round_trip(msg);
            } else {
              auto msg =
                  build_message({"del", "key" + std::to_string(ops % 100)});
              clients[i]->round_trip(msg);
            }
            ops++;
          } catch (...) {
            break;
          }
        }
        total_ops.fetch_add(ops);
      });
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    stop = true;

    for (auto& t : client_threads) {
      t.join();
    }

    state.SetIterationTime(1.0);
  }

  state.SetItemsProcessed(total_ops.load());
  state.SetLabel("EventLoop");
}

BENCHMARK_REGISTER_F(EventLoopFixture, Latency_SingleClient)
    ->UseManualTime()
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(ThreadedFixture, Latency_SingleClient)
    ->UseManualTime()
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_REGISTER_F(EventLoopFixture, Throughput_MultiClient)
    ->Arg(1)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)  // num connections
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK_REGISTER_F(ThreadedFixture, Throughput_MultiClient)
    ->Arg(1)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)  // num connections
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK_REGISTER_F(EventLoopFixture, MixedWorkload)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
