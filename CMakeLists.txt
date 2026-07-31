cmake_minimum_required(VERSION 3.22)
project(spsc_ringbuffer)
set(CMAKE_CXX_STANDARD 20)

# Off by default: enabling it makes configure download the Boost source archive
# (~100 MB), which nobody who only wants the tests should have to pay for.
option(SPSC_BENCH_BOOST "Benchmark against boost::lockfree::spsc_queue" ON)

include(FetchContent)
find_package(Threads REQUIRED)

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

# Without this no CTestTestfile.cmake is generated, so gtest_discover_tests has
# nowhere to register and `ctest` silently reports success having run nothing.
enable_testing()

include(GoogleTest)
gtest_discover_tests(spsc_tests)

# spsc_benchmarks measures throughput (queue kept full), spsc_latency_benchmarks
# measures round-trip latency (queue kept empty). Separate binaries so one can be
# run without the other's warm-up and pinning affecting the machine state.
add_executable(spsc_benchmarks benchmarks/spsc_benchmarks.cpp)
add_executable(spsc_latency_benchmarks benchmarks/spsc_latency_benchmarks.cpp)
set(SPSC_BENCH_TARGETS spsc_benchmarks spsc_latency_benchmarks)

foreach(bench_target IN LISTS SPSC_BENCH_TARGETS)
    target_include_directories(${bench_target} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(${bench_target} benchmark::benchmark_main Threads::Threads)
    target_compile_options(${bench_target} PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2>
        $<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<CONFIG:Release>>:-O3>
    )
endforeach()

if (SPSC_BENCH_BOOST)
    # Boost.Lockfree is header-only but drags in a dozen Boost libs; let Boost's
    # own CMake resolve them instead of vendoring a subset of the tree.
    set(BOOST_INCLUDE_LIBRARIES lockfree)
    set(BOOST_ENABLE_CMAKE ON)
    FetchContent_Declare(
      Boost
      URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz
      DOWNLOAD_EXTRACT_TIMESTAMP ON
    )
    FetchContent_MakeAvailable(Boost)

    foreach(bench_target IN LISTS SPSC_BENCH_TARGETS)
        target_link_libraries(${bench_target} Boost::lockfree)
        target_compile_definitions(${bench_target} PRIVATE SPSC_HAS_BOOST=1)
    endforeach()
endif()