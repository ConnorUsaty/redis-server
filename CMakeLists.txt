cmake_minimum_required(VERSION 3.10)
project(MyProject)

# Set C++ compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -g")

# Define executables
add_executable(client.exe src/client.cpp)
add_executable(server_threaded.exe src/servers/server_threaded.cpp)
add_executable(server_event-loop.exe src/servers/server_event-loop.cpp)