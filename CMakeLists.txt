cmake_minimum_required(VERSION 3.10)
project(MyProject)

# Set C++ standard to C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3 -march=native -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -flto")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)

# Define executables
add_executable(client.exe src/client.cpp)
add_executable(server_threaded.exe src/server_threaded.cpp)
add_executable(server_event-loop.exe src/server_event-loop.cpp)

# Find installed packages
find_package(GTest REQUIRED)
find_package(benchmark REQUIRED)

# Unit tests
enable_testing()

# Buffer unit test
add_executable(buffer_unit_test tests/unit/buffer_unit_test.cpp)
target_include_directories(buffer_unit_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(buffer_unit_test ${GTEST_LIBRARIES} ${GTEST_MAIN_LIBRARIES} pthread)

# Servers unit test  
add_executable(servers_unit_test tests/unit/servers_unit_test.cpp)
target_include_directories(servers_unit_test PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(servers_unit_test ${GTEST_LIBRARIES} ${GTEST_MAIN_LIBRARIES} pthread)

# Performance benchmarks
add_executable(servers_benchmark tests/perf/servers_benchmark.cpp)
target_include_directories(servers_benchmark PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(servers_benchmark benchmark::benchmark pthread)

# Add tests to CTest
add_test(NAME BufferUnitTest COMMAND buffer_unit_test)
add_test(NAME ServersUnitTest COMMAND servers_unit_test)
