cmake_minimum_required(VERSION 3.16)
project(etrade_pipeline LANGUAGES CXX)

# Use C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebIfo)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused_paramter)
endif()

# Include header
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Create executable from source file
add_executable(udp_receiver 
    src/udp_receiver.cpp
    src/order_book.cpp
    src/order_manager.cpp
    src/risk.cpp
)
# Compiler flags: optimize for a development build and tune later for performance
target_compile_options(udp_receiver PRIVATE -O2 -g -Wall -Wextra -Wpedantic)


add_executable(execution_gateway
    src/execution_gateway.cpp
    src/order_book.cpp
    src/order_manager.cpp
    src/risk.cpp
)
# link pthread explicitly if your environment needs it for threads
find_package(Threads REQUIRED)
target_link_libraries(execution_gateway PRIVATE Threads::Threads)
target_include_directories(execution_gateway PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(engine
    src/engine.cpp
    src/udp_receiver.cpp
    src/execution_gateway.cpp
    src/order_book.cpp
    src/order_manager.cpp
    src/risk.cpp
)
target_link_libraries(engine PRIVATE  Threads::Threads)
target_include_directories(engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(engine PRIVATE ENGINE_BUILD)