#pragma once

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

/* Buffer structure optimized for our exact use case
 * Allows for more efficient consumes and clears than std::vector */

struct alignas(64) Buffer {
  uint8_t* buf_start_;
  uint8_t* buf_end_;
  uint8_t* data_start_;
  uint8_t* data_end_;

  Buffer(size_t sz) {
    // round to nearest multiple of 64
    sz = (sz + 63U) & ~63U;

    buf_start_ = static_cast<uint8_t*>(std::aligned_alloc(64, sz));
    buf_end_ = buf_start_ + sz;
    data_start_ = buf_start_;
    data_end_ = buf_start_;
  }

  ~Buffer() { std::free(buf_start_); }

  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer(Buffer&&) = delete;
  Buffer& operator=(Buffer&&) = delete;

  inline size_t size() const noexcept {
    return static_cast<size_t>(data_end_ - data_start_);
  }

  inline size_t capacity() const noexcept {
    return static_cast<size_t>(buf_end_ - buf_start_);
  }

  inline uint8_t* data() const noexcept { return data_start_; }

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

  inline void clear() noexcept { data_start_ = data_end_ = buf_start_; }

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
      if (avail_back + avail_front < total_req) [[unlikely]] {
        // just re-allocate
        size_t buf_sz = buf_end_ - buf_start_;
        size_t data_sz = data_end_ - data_start_;
        while (buf_sz < data_sz + total_req) {
          buf_sz <<= 1;
        }

        // buf_sz is already guarenteed to be a multiple of 64
        uint8_t* t = static_cast<uint8_t*>(std::aligned_alloc(64, buf_sz));
        memcpy(t, data_start_, data_sz);
        std::free(buf_start_);

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
