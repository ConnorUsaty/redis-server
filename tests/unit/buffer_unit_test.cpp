#include <gtest/gtest.h>

#include <random>

#include "buffer.h"

class BufferTest : public ::testing::Test {
 protected:
  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(BufferTest, ConstructionAlignmentTest) {
  // test that buffer size is rounded up to nearest 64 byte multiple
  Buffer buf1(1);
  EXPECT_EQ(buf1.capacity(), 64);

  Buffer buf2(63);
  EXPECT_EQ(buf2.capacity(), 64);

  Buffer buf3(65);
  EXPECT_EQ(buf3.capacity(), 128);

  Buffer buf4(256);
  EXPECT_EQ(buf4.capacity(), 256);
}

TEST_F(BufferTest, BasicAppendConsumeTest) {
  Buffer buf(128);
  EXPECT_EQ(buf.capacity(), 128);

  uint8_t data[] = {1, 2, 3, 4, 5};
  buf.append(data, 5);

  EXPECT_EQ(buf.size(), 5);
  for (size_t i = 0; i < buf.size(); ++i) {
    EXPECT_EQ(buf.data()[i], data[i]);
  }

  buf.consume(2);
  EXPECT_EQ(buf.size(), 3);
  EXPECT_EQ(buf.data()[0], 3);

  buf.consume(3);
  EXPECT_EQ(buf.size(), 0);
  EXPECT_EQ(buf.capacity(), 128);
}

TEST_F(BufferTest, ClearTest) {
  Buffer buf(128);
  EXPECT_EQ(buf.capacity(), 128);

  uint8_t data[] = {1, 2, 3, 4, 5};
  buf.append(data, 5);

  EXPECT_EQ(buf.size(), 5);
  buf.clear();
  EXPECT_EQ(buf.size(), 0);
  EXPECT_EQ(buf.capacity(), 128);
}

TEST_F(BufferTest, CompactionTest) {
  Buffer buf(128);

  // fill buffer partially
  uint8_t data1[40] = {};
  std::fill(data1, data1 + 40, 1);
  buf.append(data1, 40);

  // consume some to create front space
  buf.consume(30);
  EXPECT_EQ(buf.size(), 10);

  // append data that requires compaction but not reallocation
  uint8_t data2[80] = {};
  std::fill(data2, data2 + 80, 2);
  buf.append(data2, 80);

  EXPECT_EQ(buf.size(), 90);
  // verify data integrity after compaction
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_EQ(buf.data()[i], 1);
  }
  for (size_t i = 10; i < 90; ++i) {
    EXPECT_EQ(buf.data()[i], 2);
  }
}

TEST_F(BufferTest, ReallocationTest) {
  Buffer buf(64);

  // force reallocation by appending more than capacity
  uint8_t data[100] = {};
  for (size_t i = 0; i < 100; ++i) {
    data[i] = i % 256;
  }

  buf.append(data, 100);

  EXPECT_GE(buf.capacity(), 104);  // 100 + 4 for the protocol
  EXPECT_EQ(buf.size(), 100);

  // verify data integrity after reallocation
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_EQ(buf.data()[i], i % 256);
  }

  // verify new capacity is multiple of 64
  EXPECT_EQ(buf.capacity() % 64, 0);
}

TEST_F(BufferTest, StressTest) {
  Buffer buf(64);
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> size_dist(1, 1000);
  std::uniform_int_distribution<int> op_dist(0, 2);

  size_t total_size = 0;
  std::vector<uint8_t> reference_data;

  for (size_t iter = 0; iter < 1000; ++iter) {
    int op = op_dist(rng);

    if (op == 0 || total_size == 0) {  // append
      size_t append_size = size_dist(rng);
      std::vector<uint8_t> data(append_size);
      for (size_t i = 0; i < append_size; ++i) {
        data[i] = (iter + i) % 256;
      }

      buf.append(data.data(), append_size);
      reference_data.insert(reference_data.end(), data.begin(), data.end());
      total_size += append_size;

    } else if (op == 1 && total_size > 0) {  // consume
      int consume_size = std::min(static_cast<int>(total_size), size_dist(rng));
      buf.consume(consume_size);
      reference_data.erase(reference_data.begin(),
                           reference_data.begin() + consume_size);
      total_size -= consume_size;

    } else if (op == 2) {  // clear
      buf.clear();
      reference_data.clear();
      total_size = 0;
    }

    // verify consistency
    ASSERT_EQ(buf.size(), total_size);
    ASSERT_EQ(buf.size(), reference_data.size());
    for (size_t i = 0; i < total_size; ++i) {
      ASSERT_EQ(buf.data()[i], reference_data[i]);
    }
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}