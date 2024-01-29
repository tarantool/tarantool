set(TARANTOOL_BIN $<TARGET_FILE:tarantool>)
set(RUN_PERF_LUA_TESTS_LIST "")

function(create_perf_lua_test)
  set(prefix PERF)
  set(noValues)
  set(singleValues NAME)
  set(multiValues)

  # FIXME: if we update to CMake >= 3.5, can remove this line.
  include(CMakeParseArguments)
  cmake_parse_arguments(${prefix}
                        "${noValues}"
                        "${singleValues}"
                        "${multiValues}"
                        ${ARGN})

  message(STATUS "Creating Lua performance test ${PERF_NAME}_perftest")
  set(TEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/${PERF_NAME}.lua)
  add_custom_target(${PERF_NAME}_perftest
                    COMMAND ${TARANTOOL_BIN} ${TEST_PATH}
                    COMMENT Running ${PERF_NAME}_perftest
                    DEPENDS tarantool ${TEST_PATH}
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
  set(RUN_PERF_LUA_TESTS_LIST ${RUN_PERF_LUA_TESTS_LIST} ${PERF_NAME}_perftest PARENT_SCOPE)
endfunction()

create_perf_lua_test(NAME 1mops_write)
create_perf_lua_test(NAME box_select)
create_perf_lua_test(NAME column_scan)
create_perf_lua_test(NAME uri_escape_unescape)

include_directories(${MSGPUCK_INCLUDE_DIRS})

build_module(column_scan_module column_scan_module.c)
target_link_libraries(column_scan_module msgpuck)
add_dependencies(column_scan_perftest column_scan_module)

add_custom_target(test-lua-perf
                  DEPENDS "${RUN_PERF_LUA_TESTS_LIST}"
                  COMMENT "Running Lua performance tests"
)
