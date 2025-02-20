include_directories(${MSGPUCK_INCLUDE_DIRS})
build_module(sql_datetime sql_datetime.c)
target_link_libraries(sql_datetime msgpuck core)
build_module(sql_interval sql_interval.c)
target_link_libraries(sql_interval msgpuck core)
build_module(gh_6572 gh_6572_nan_is_not_null_test.c)
target_link_libraries(gh_6572 msgpuck core)
build_module(gh_10132 gh_10132_mp_int_bugs.c)

tarantool_make_lua_path(LUA_CPATH
  PATHS
  ${CMAKE_CURRENT_BINARY_DIR}/?${CMAKE_SHARED_LIBRARY_SUFFIX}
)

set(TEST_SUITE_NAME "sql-luatest")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

# XXX: The call produces both test and target
# <sql-luatest-deps> as a side effect.
_add_test_suite_target(${TEST_SUITE_NAME}
  LABELS "${TEST_SUITE_NAME};regression;"
  DEPENDS sql_datetime sql_interval gh_6572
)

list(APPEND TEST_ENV
  "LUA_PATH=${LUATEST_LUA_PATH}"
  "LUA_CPATH=${LUA_CPATH}"
  "BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}"
  "VARDIR=${VARDIR}"
)

file(GLOB tests ${CMAKE_CURRENT_SOURCE_DIR} *_test.lua)
foreach(test_path ${tests})
  get_filename_component(test_name ${test_path} NAME)
  # FIXME: By default, GLOB lists directories.
  # Directories are omitted in the result if LIST_DIRECTORIES
  # is set to false. New in version CMake 3.3.
  if(${test_name} STREQUAL ${TEST_SUITE_NAME})
    continue()
  endif()
  set(test_title "test/${TEST_SUITE_NAME}/${test_name}")
  add_test(NAME ${test_title}
           COMMAND ${TARANTOOL_BIN}
                   # Disable stdout buffering,
                   # see https://github.com/tarantool/test-run/issues/119.
                   -e "io.stdout:setvbuf('no')"
                   ${LUATEST_COMMAND_WITH_FLAGS}
                   ${test_title}
           WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
  set_tests_properties(${test_title} PROPERTIES
    ENVIRONMENT "${TEST_ENV}"
    LABELS "${TEST_SUITE_NAME};regression;"
    DEPENDS ${TEST_SUITE_NAME}-deps
    FAIL_REGULAR_EXPRESSION "${CTEST_FAIL_REGEXP}"
  )
endforeach()

# See test/sql-luatest/suite.ini.
set_tests_properties(
  test/sql-luatest/ghs_119_too_long_mem_values_test.lua
  test/sql-luatest/ghs_122_allocations_in_printf_test.lua
  PROPERTIES LABELS "long"
)
