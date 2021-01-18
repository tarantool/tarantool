set(CMAKE_CXX_STANDARD 11)

set(PERF_TESTS_OPTION ON)

if (NOT DEFINED WITH_UNIT_PERF_TESTS)
    set(PERF_TESTS_OPTION OFF)
endif()

if (PERF_TESTS_OPTION AND NOT WITH_UNIT_PERF_TESTS)
    return()
endif()

find_package(benchmark QUIET)
if (NOT ${benchmark_FOUND})
    if (PERF_TESTS_OPTION AND WITH_UNIT_PERF_TESTS)
        message(FATAL_ERROR "Google Benchmark submodule not found")
    else()
        message(AUTHOR_WARNING "Google Benchmark submodule not found")
    endif()
    return()
endif()

file(GLOB all_sources *.c *.cc)

if(NOT TARGET_OS_OPENBSD)
    set(LIB_DL "dl")
endif()

include_directories(${MSGPUCK_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/src/box)
include_directories(${CMAKE_SOURCE_DIR}/third_party)

add_executable(tuple.perftest tuple.cc)
target_link_libraries(tuple.perftest core box tuple benchmark::benchmark)
