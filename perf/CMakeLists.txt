set(CMAKE_CXX_STANDARD 14)

add_subdirectory(lua)

find_package(benchmark QUIET)
if (NOT ${benchmark_FOUND})
    message(AUTHOR_WARNING "Google Benchmark library was not found")
    set(MSG "Target test-c-perf is dummy, Google Benchmark library was not found")
    add_custom_target(test-c-perf
                      COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red ${MSG}
                      COMMENT ${MSG}
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
  add_custom_target(${PERF_TARGET}_perftest
                    COMMAND "$<TARGET_FILE:${PERF_TARGET}.perftest>"
                    DEPENDS ${PERF_TARGET}.perftest
                    COMMENT Running ${PERF_TARGET}_perftest
  )
  set(RUN_PERF_C_TESTS_LIST ${RUN_PERF_C_TESTS_LIST} ${PERF_TARGET}_perftest PARENT_SCOPE)
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

create_perf_test(NAME light
                 SOURCES light.cc ${PROJECT_SOURCE_DIR}/test/unit/box_test_utils.c
                 LIBRARIES small benchmark::benchmark
)
create_perf_test_target(TARGET light)

create_perf_test_target(TARGET small)

add_custom_target(test-c-perf
                  DEPENDS ${RUN_PERF_C_TESTS_LIST}
                  COMMENT "Running C performance tests"
)
