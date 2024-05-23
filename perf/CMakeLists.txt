set(CMAKE_CXX_STANDARD 14)

set(PERF_OUTPUT_DIR ${PROJECT_BINARY_DIR}/perf/output)
file(MAKE_DIRECTORY ${PERF_OUTPUT_DIR})

set(BENCH_RESULTS "")

add_subdirectory(lua)

set(TARANTOOL_BIN $<TARGET_FILE:tarantool>)

find_package(benchmark QUIET)
if (NOT ${benchmark_FOUND})
    message(AUTHOR_WARNING "Google Benchmark library was not found")
    set(MSG "Target test-c-perf is dummy, Google Benchmark library was not found")
    add_custom_target(test-c-perf
                      COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red ${MSG}
                      COMMENT ${MSG}
    )
    add_custom_target(test-perf
                      DEPENDS test-c-perf test-lua-perf
                      COMMENT "Running performance tests"
    )
    return()
endif()

include_directories(${MSGPUCK_INCLUDE_DIRS})
include_directories(${PROJECT_SOURCE_DIR}/src/box)
include_directories(${PROJECT_SOURCE_DIR}/third_party)
include_directories(${EXTRA_CORE_INCLUDE_DIRS})

set(RUN_PERF_C_TESTS_LIST "")

function(create_perf_test_target)
  set(prefix PERF)
  set(noValues)
  set(singleValues TARGET)
  set(multiValues)

  # FIXME: if we update to CMake >= 3.5, can remove this line.
  include(CMakeParseArguments)
  cmake_parse_arguments(${prefix}
                        "${noValues}"
                        "${singleValues}"
                        "${multiValues}"
                        ${ARGN})
  message(STATUS "Creating C performance test ${PERF_TARGET}_perftest")

  set(BENCH_RESULT ${PERF_OUTPUT_DIR}/${PERF_TARGET}.json)
  set(BENCH_TARGET ${PERF_TARGET}_perftest)
  set(BENCH_RESULT_TARGET ${BENCH_TARGET}_result)

  # XXX: We need to provide two different targets with the same
  # command: the first (BENCH_TARGET) is run unconditionally
  # regardless of whether there are files with benchmark results
  # or not, and the second target (BENCH_RESULT_TARGET) is run
  # only if the corresponding file is omitted. The COMMAND_LIST
  # variable contains the same command for these targets.
  set(COMMAND_LIST
        COMMAND "$<TARGET_FILE:${PERF_TARGET}.perftest>"
                "--benchmark_out_format=json"
                "--benchmark_out=${BENCH_RESULT}"
        DEPENDS ${PERF_TARGET}.perftest
        COMMENT Running ${BENCH_TARGET}
  )
  add_custom_command(OUTPUT ${BENCH_RESULT} ${COMMAND_LIST})
  add_custom_target(${BENCH_RESULT_TARGET} DEPENDS ${BENCH_RESULT})
  add_custom_target(${BENCH_TARGET} ${COMMAND_LIST})

  set(RUN_PERF_C_TESTS_LIST ${RUN_PERF_C_TESTS_LIST} ${BENCH_TARGET} PARENT_SCOPE)
  set(BENCH_RESULTS ${BENCH_RESULT_TARGET} ${BENCH_RESULTS} PARENT_SCOPE)
endfunction()

function(create_perf_test)
  set(prefix PERF)
  set(noValues)
  set(singleValues NAME)
  set(multiValues "SOURCES;LIBRARIES")

  # FIXME: if we update to CMake >= 3.5, can remove this line.
  include(CMakeParseArguments)
  cmake_parse_arguments(${prefix}
                        "${noValues}"
                        "${singleValues}"
                        "${multiValues}"
                        ${ARGN})
  add_executable(${PERF_NAME}.perftest ${PERF_SOURCES})
  target_link_libraries(${PERF_NAME}.perftest PUBLIC ${PERF_LIBRARIES})
endfunction()

create_perf_test(NAME tuple
                 SOURCES tuple.cc ${PROJECT_SOURCE_DIR}/test/unit/box_test_utils.c
                 LIBRARIES core box tuple benchmark::benchmark
)
create_perf_test_target(TARGET tuple)

create_perf_test(NAME bps_tree
                 SOURCES bps_tree.cc ${PROJECT_SOURCE_DIR}/test/unit/box_test_utils.c
                 LIBRARIES core box tuple benchmark::benchmark
)
create_perf_test_target(TARGET bps_tree)

create_perf_test(NAME light
                 SOURCES light.cc ${PROJECT_SOURCE_DIR}/test/unit/box_test_utils.c
                 LIBRARIES small benchmark::benchmark
)
create_perf_test_target(TARGET light)

create_perf_test_target(TARGET small)

create_perf_test(NAME memtx
                 SOURCES memtx.cc ${PROJECT_SOURCE_DIR}/test/unit/box_test_utils.c
                 LIBRARIES core box server benchmark::benchmark
)
create_perf_test_target(TARGET memtx)

add_custom_target(test-c-perf
                  DEPENDS ${RUN_PERF_C_TESTS_LIST}
                  COMMENT "Running C performance tests"
)

add_custom_target(test-perf
                  DEPENDS test-c-perf test-lua-perf
                  COMMENT "Running performance tests"
)


set(PERF_SUMMARY ${PERF_OUTPUT_DIR}/summary.txt)
add_custom_target(test-perf-aggregate
                  DEPENDS ${BENCH_RESULTS}
                  BYPRODUCTS ${PERF_SUMMARY}
                  COMMENT "Aggregate performance test results into ${PERF_SUMMARY}"
                  COMMAND ${TARANTOOL_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/tools/aggregate.lua
                    --output=${PERF_SUMMARY}
                    --input_dir=${PERF_OUTPUT_DIR}
                  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
