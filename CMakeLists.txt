cmake_minimum_required(VERSION 3.22)
project(spsc_ringbuffer)

set(CMAKE_CXX_STANDARD 20)

add_executable(spsc_ringbuffer src/main.cpp
        include/SpscRingBuffer.hpp)

target_include_directories(spsc_ringbuffer PRIVATE ${CMAKE_SOURCE_DIR}/include)