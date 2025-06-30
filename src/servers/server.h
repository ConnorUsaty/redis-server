#pragma once

#include <iostream>
#include <string>
#include <vector>

struct Buffer {
  /* Buffer structure optimized for our exact use case
   * Allows for more efficient consumes and clears than std::vector */

  uint8_t* buf_start_;
  uint8_t* buf_end_;
  uint8_t* data_start_;
  uint8_t* data_end_;

  Buffer(size_t sz) {
    buf_start_ = new uint8_t[sz];
    buf_end_ = buf_start_ + sz;
    data_start_ = buf_start_;
    data_end_ = buf_start_;
  }

  ~Buffer() { delete[] buf_start_; }

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer(Buffer&&) = delete;
  Buffer& operator=(Buffer&&) = delete;

  size_t size() const { return static_cast<size_t>(data_end_ - data_start_); }

  size_t capacity() const { return static_cast<size_t>(buf_end_ - buf_start_); }

  uint8_t* data() const { return data_start_; }

  // for debugging purposes
  void print_data() const {
    uint8_t* p = data_start_;
    std::cout << "Buffer: {";
    while (p != data_end_) {
      std::cout << *p;
      p++;
    }
    std::cout << "}\n";
  }

  void clear() { data_start_ = data_end_ = buf_start_; }

  void consume(size_t sz) {
    assert(sz <= size());
    data_start_ += sz;
    if (data_start_ == data_end_) {
      // Reset to beginning when empty
      data_start_ = data_end_ = buf_start_;
    }
  }

  void append(uint8_t* msg, uint32_t msg_len) {
    assert(msg_len > 0);
    size_t avail_back = buf_end_ - data_end_;
    size_t avail_front = data_start_ - buf_start_;
    size_t total_req = msg_len + 4;

    if (avail_back < total_req) {
      // response cannot fit in back of buffer
      // we can get more room by either ensuring data_start_ = buf_start_
      // which will give us (data_start_ - buf_start_) more bytes
      // or we can entirely re-allocate a larger buffer
      if (avail_back + avail_front < total_req) {
        // just re-allocate
        size_t buf_sz = buf_end_ - buf_start_;
        size_t data_sz = data_end_ - data_start_;
        while (buf_sz < data_sz + total_req) {
          buf_sz <<= 1;
        }

        uint8_t* t = new uint8_t[buf_sz];
        memcpy(t, data_start_, data_sz);
        delete[] buf_start_;

        buf_start_ = t;
        buf_end_ = buf_start_ + buf_sz;
        data_start_ = buf_start_;
        data_end_ = data_start_ + data_sz;
      } else {
        // reset data_start_ to buf_start_
        size_t data_len = static_cast<size_t>(data_end_ - data_start_);
        memcpy(buf_start_, data_start_, data_len);
        data_start_ = buf_start_;
        data_end_ = data_start_ + data_len;
      }
    }

    // now we have ensured there is room at the back and can add the msg
    memcpy(data_end_, msg, msg_len);
    data_end_ += msg_len;
  }
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
