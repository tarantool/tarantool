set(CMAKE_CXX_STANDARD 11)

find_package(benchmark QUIET)
if (NOT ${benchmark_FOUND})
    message(AUTHOR_WARNING "Google Benchmark library was not found")
    return()
endif()

include_directories(${MSGPUCK_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/src/box)
include_directories(${PROJECT_SOURCE_DIR}/third_party)

add_executable(tuple.perftest tuple.cc)
target_link_libraries(tuple.perftest core box tuple benchmark::benchmark)
