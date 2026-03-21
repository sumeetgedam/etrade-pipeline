cmake_minimum_required(VERSION 3.16)
project(etrade_pipeline LANGUAGES CXX)

# Use C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include header
include_directories(${CMAKE_SOURCE_DIR}/include)

# Create executable from source file
add_executable(udp_receiver 
    src/udp_receiver.cpp
    src/order_book.cpp
)

add_executable(execution_gateway
    src/execution_gateway.cpp
)

# Compiler flags: optimize for a development build and tune later for performance
target_compile_options(udp_receiver PRIVATE -O2 -g -Wall -Wextra -Wpedantic)

target_include_directories(execution_gateway PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
)

# link pthread explicitly if your environment needs it for threads
find_package(Threads REQUIRED)
target_link_libraries(execution_gateway PRIVATE Threads::Threads)