enable_tnt_compile_flags()

set(LUA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/?.lua\;\;")
set(TEST_SUITE_NAME "performance-lua")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

function(create_perf_lua_test)
  set(prefix PERF)
  set(noValues)
  set(singleValues NAME)
  set(multiValues DEPENDS)

  # FIXME: if we update to CMake >= 3.5, can remove this line.
  include(CMakeParseArguments)
  cmake_parse_arguments(${prefix}
                        "${noValues}"
                        "${singleValues}"
                        "${multiValues}"
                        ${ARGN})

  set(TEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/${PERF_NAME}.lua)
  set(BENCH_RESULT ${PERF_OUTPUT_DIR}/${PERF_NAME}.json)
  set(BENCH_TARGET ${PERF_NAME}_perftest)
  set(BENCH_RESULT_TARGET ${BENCH_TARGET}_result)

  # XXX: We need to provide two different targets with the same
  # command: the first (BENCH_TARGET) is run unconditionally
  # regardless of whether there are files with benchmark results
  # or not, and the second target (BENCH_RESULT_TARGET) is run
  # only if the corresponding file is omitted. The COMMAND_LIST
  # variable contains the same command for these targets.
  set(TEST_DEPS tarantool ${PERF_DEPENDS} ${TEST_PATH})
  set(COMMAND_LIST
        COMMENT Running ${BENCH_TARGET}
        COMMAND ${CMAKE_COMMAND} -E env
          LUA_PATH="${LUA_PATH}"
          ${BENCH_CMD_SEPARATE} ${TARANTOOL_BIN} ${TEST_PATH}
            --output="${BENCH_RESULT}"
            --output_format=json
        DEPENDS ${TEST_DEPS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )

  add_custom_command(OUTPUT ${BENCH_RESULT} ${COMMAND_LIST})
  add_custom_target(${BENCH_RESULT_TARGET} DEPENDS ${BENCH_RESULT})
  add_custom_target(${BENCH_TARGET} ${COMMAND_LIST})

  # XXX: The call produces both test and target
  # <${PERF_NAME}-deps> as a side effect.
  _add_test_suite_target(${PERF_NAME}
    LABELS "${TEST_SUITE_NAME};performance;"
    DEPENDS ${TEST_DEPS})
  set(BENCH_RESULTS ${BENCH_RESULT_TARGET} ${BENCH_RESULTS}  PARENT_SCOPE)
  set(test_title "perf/lua/${PERF_NAME}.lua")
  add_test(NAME ${test_title}
           COMMAND ${TARANTOOL_BIN}
                   ${CMAKE_CURRENT_SOURCE_DIR}/${PERF_NAME}.lua
                   --output=${BENCH_RESULT} --output_format=json
  )
  set_tests_properties(${test_title} PROPERTIES
    ENVIRONMENT "LUA_PATH=${LUA_PATH}"
    LABELS "${TEST_SUITE_NAME};performance;"
    DEPENDS ${BENCH_TARGET}-deps)
endfunction()

create_perf_lua_test(NAME box_select)
create_perf_lua_test(NAME gh-7089-vclock-copy)
create_perf_lua_test(NAME luafun)
create_perf_lua_test(NAME tuple_field_by_name)
create_perf_lua_test(NAME uri_escape_unescape)

create_perf_lua_test(NAME recovery)
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/recovery_script.lua
              INPUT ${CMAKE_CURRENT_SOURCE_DIR}/recovery_script.lua)

create_perf_lua_test(NAME box_snapshot)

include_directories(${MSGPUCK_INCLUDE_DIRS})

build_module(1mops_write_module 1mops_write_module.c)
target_link_libraries(1mops_write_module msgpuck)
create_perf_lua_test(NAME 1mops_write
                     DEPENDS 1mops_write_module
)

# The default workload runs the Lua path. The C fiber is the path the
# benchmark exists for, so it is measured in a case of its own. The
# result goes to a subdirectory so that the C fiber run does not add a
# second point to the benchmark's series in the monitoring.
set(C_FIBER_OUTPUT_DIR ${PERF_OUTPUT_DIR}/1mops_write-c-fiber)
file(MAKE_DIRECTORY ${C_FIBER_OUTPUT_DIR})
add_test(NAME perf/lua/1mops_write-c-fiber
         COMMAND ${TARANTOOL_BIN}
                 ${CMAKE_CURRENT_SOURCE_DIR}/1mops_write.lua
                 --c_fiber --columns=10 --ops=20000 --fibers=4
                 --transaction=10 --warmup=0 --wal_mode=none
                 --output=${C_FIBER_OUTPUT_DIR}/1mops_write.json
                 --output_format=json
)
set_tests_properties(perf/lua/1mops_write-c-fiber PROPERTIES
  ENVIRONMENT "LUA_PATH=${LUA_PATH};BUILDDIR=${CMAKE_BINARY_DIR}"
  LABELS "${TEST_SUITE_NAME};performance;"
  DEPENDS 1mops_write-deps)

build_module(column_scan_module column_scan_module.c)
target_link_libraries(column_scan_module msgpuck)
create_perf_lua_test(NAME column_scan
                     DEPENDS column_scan_module
)

build_module(column_insert_module column_insert_module.c)
target_link_libraries(column_insert_module msgpuck)
create_perf_lua_test(NAME column_insert
                     DEPENDS column_insert_module
)

if(ENABLE_READ_VIEW)
  create_perf_lua_test(NAME read_view_open_close)
endif()

add_custom_target(test-lua-perf
                  COMMAND ${CMAKE_CTEST_COMMAND}
                          ${TARANTOOL_PERF_CTEST_FLAGS}
                          -L ${TEST_SUITE_NAME}
                  COMMENT "Running Lua performance tests"
)

# Propagate the list to the parent scope.
set(BENCH_RESULTS "${BENCH_RESULTS}" PARENT_SCOPE)
