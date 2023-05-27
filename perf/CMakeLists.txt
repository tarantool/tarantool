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

function(create_perf_test)
  cmake_parse_arguments(
    PERF
    ""
    "PREFIX"
    "SOURCES;LIBRARIES"
    ${ARGN}
  )
  message(STATUS "Creating performance test ${PERF_PREFIX}.perftest")
  add_executable(${PERF_PREFIX}.perftest ${PERF_SOURCES})
  target_link_libraries(${PERF_PREFIX}.perftest PUBLIC ${PERF_LIBRARIES})
endfunction()

create_perf_test(PREFIX tuple
                 SOURCES tuple.cc
                 LIBRARIES core box tuple benchmark::benchmark
)

create_perf_test(PREFIX light
                 SOURCES light.cc
                 LIBRARIES small benchmark::benchmark
)
