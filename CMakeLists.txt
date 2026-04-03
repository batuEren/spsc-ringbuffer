cmake_minimum_required(VERSION 3.22)
project(spsc_ringbuffer)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
  benchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.zip
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

add_executable(spsc_ringbuffer src/main.cpp)
target_include_directories(spsc_ringbuffer PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(spsc_tests tests/spsc_tests.cpp)
target_include_directories(spsc_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(spsc_tests GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(spsc_tests)

add_executable(spsc_benchmarks benchmarks/spsc_benchmarks.cpp)
target_include_directories(spsc_benchmarks PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(spsc_benchmarks benchmark::benchmark_main)