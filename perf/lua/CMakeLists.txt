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

create_perf_lua_test(NAME 1mops_write)
create_perf_lua_test(NAME box_select)
create_perf_lua_test(NAME gh-7089-vclock-copy)
create_perf_lua_test(NAME luafun)
create_perf_lua_test(NAME tuple_field_by_name)
create_perf_lua_test(NAME uri_escape_unescape)

include_directories(${MSGPUCK_INCLUDE_DIRS})

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

add_custom_target(test-lua-perf
                  COMMAND ${CMAKE_CTEST_COMMAND}
                          ${TARANTOOL_PERF_CTEST_FLAGS}
                          -L ${TEST_SUITE_NAME}
                  COMMENT "Running Lua performance tests"
)

# Propagate the list to the parent scope.
set(BENCH_RESULTS "${BENCH_RESULTS}" PARENT_SCOPE)
