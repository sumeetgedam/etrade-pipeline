cmake_minimum_required(VERSION 3.16)
project(udp_receiver LANGUAGES CXX)

# Use C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Create executable frm source file
add_executable(udp_receiver src/udp_receiver.cpp)

# Compiler flags: optimize for a development build and tune later for performance
target_compile_options(udp_receiver PRIVATE -O2 -g -Wall -Wextra -Wpedantic)