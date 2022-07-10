set(CMAKE_CXX_STANDARD 14)

find_package(benchmark QUIET)
if (NOT ${benchmark_FOUND})
    message(AUTHOR_WARNING "Google Benchmark library was not found")
    return()
endif()

include_directories(${MSGPUCK_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/src/box)
include_directories(${PROJECT_SOURCE_DIR}/third_party)
include_directories(${EXTRA_CORE_INCLUDE_DIRS})

add_executable(tuple.perftest tuple.cc
               ${PROJECT_SOURCE_DIR}/test/unit/box_test_utils.c)
target_link_libraries(tuple.perftest core box tuple benchmark::benchmark)

add_executable(light.perftest light.cc
               ${PROJECT_SOURCE_DIR}/test/unit/box_test_utils.c)
target_link_libraries(light.perftest small benchmark::benchmark)
