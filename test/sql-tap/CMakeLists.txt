include_directories(${MSGPUCK_INCLUDE_DIRS})
build_module(gh-5938-wrong-string-length gh-5938-wrong-string-length.c)
target_link_libraries(gh-5938-wrong-string-length msgpuck)
build_module(gh-6024-funcs-return-bin gh-6024-funcs-return-bin.c)
target_link_libraries(gh-6024-funcs-return-bin msgpuck)
build_module(gh-2579-custom-aggregate gh-2579-custom-aggregate.c)
target_link_libraries(gh-2579-custom-aggregate msgpuck)
build_module(sql_uuid sql_uuid.c)
target_link_libraries(sql_uuid msgpuck core)
build_module(decimal decimal.c)
target_link_libraries(decimal msgpuck)

tarantool_make_lua_path(LUA_PATH
  PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/lua/?.lua
    ${PROJECT_SOURCE_DIR}/test/sql/lua/?.lua
    # Needed for identifier.lua.
    ${PROJECT_SOURCE_DIR}/test/box/lua/?.lua
)

tarantool_make_lua_path(LUA_CPATH
  PATHS
  ${CMAKE_CURRENT_BINARY_DIR}/?${CMAKE_SHARED_LIBRARY_SUFFIX}
)

set(TEST_SUITE_NAME "sql-tap")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

# XXX: The call produces both test and target <sql-tap-deps>
# as a side effect.
_add_test_suite_target(${TEST_SUITE_NAME}
  LABELS ${TEST_SUITE_NAME}
  DEPENDS gh-5938-wrong-string-length
          gh-6024-funcs-return-bin
          gh-2579-custom-aggregate
          sql_uuid decimal
)

list(APPEND TEST_ENV
  "LUA_PATH=${LUA_PATH}"
  "LUA_CPATH=${LUA_CPATH}"
  "BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}"
)

file(GLOB tests ${CMAKE_CURRENT_SOURCE_DIR} *.test.lua)
foreach(test_path ${tests})
  get_filename_component(test_name ${test_path} NAME)
  # FIXME: By default, GLOB lists directories.
  # Directories are omitted in the result if LIST_DIRECTORIES
  # is set to false. New in version CMake 3.3.
  if(${test_name} STREQUAL ${TEST_SUITE_NAME})
    continue()
  endif()
  set(TEST_TITLE "test/${TEST_SUITE_NAME}/${test_name}${TEST_PARAM_DELIM}memtx")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/${test_name}-workdir-memtx")
  file(MAKE_DIRECTORY ${WORK_DIR})
  set(TEST_CMD "${RM} -rf ${WORK_DIR}/* && ${TARANTOOL_BIN} ${test_path}")
  add_test(NAME ${TEST_TITLE}
           COMMAND ${BASH} -c "${TEST_CMD}"
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_tests_properties(${TEST_TITLE} PROPERTIES
    ENVIRONMENT "${TEST_ENV};TT_WORK_DIR=${WORK_DIR};TEST_ENGINE=memtx;"
    LABELS "${TEST_SUITE_NAME};regression"
    DEPENDS ${TEST_SUITE_NAME}-deps
    FAIL_REGULAR_EXPRESSION "${CTEST_FAIL_REGEXP}"
  )

  set(TEST_TITLE "test/${TEST_SUITE_NAME}/${test_name}${TEST_PARAM_DELIM}vinyl")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/${test_name}-workdir-vinyl")
  file(MAKE_DIRECTORY ${WORK_DIR})
  set(TEST_CMD "${RM} -rf ${WORK_DIR}/* && ${TARANTOOL_BIN} ${TAP_TEST_FLAGS} ${test_path}")
  add_test(NAME ${TEST_TITLE}
           COMMAND ${BASH} -c "${TEST_CMD}"
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_tests_properties(${TEST_TITLE} PROPERTIES
    ENVIRONMENT "${TEST_ENV};TT_WORK_DIR=${WORK_DIR};TEST_ENGINE=vinyl;"
    LABELS "${TEST_SUITE_NAME};regression"
    DEPENDS ${TEST_SUITE_NAME}-deps
    FAIL_REGULAR_EXPRESSION "${CTEST_FAIL_REGEXP}"
  )
endforeach()

# See test/sql-tap/suite.ini.
list(APPEND DISABLED_TESTS
  test/sql-tap/analyze1.test.lua
  test/sql-tap/analyze3.test.lua
  test/sql-tap/analyze4.test.lua
  test/sql-tap/analyze5.test.lua
  test/sql-tap/analyze6.test.lua
  test/sql-tap/analyze7.test.lua
  test/sql-tap/analyze8.test.lua
  test/sql-tap/analyze9.test.lua
  test/sql-tap/analyzeC.test.lua
  test/sql-tap/analyzeD.test.lua
  test/sql-tap/analyzeE.test.lua
  test/sql-tap/analyzeF.test.lua
  test/sql-tap/date.test.lua
  # See gh-3694.
  test/sql-tap/debug_mode_only.test.lua
  test/sql-tap/e_expr.test.lua
  test/sql-tap/gh-3350-skip-scan.test.lua
  test/sql-tap/like2.test.lua
  test/sql-tap/randexpr1.test.lua
  test/sql-tap/selectA.test.lua
  test/sql-tap/tkt3791.test.lua
  test/sql-tap/tkt-bd484a090c.test.lua
  test/sql-tap/types2.test.lua
)
foreach(test_name IN LISTS DISABLED_TESTS)
  set_tests_properties(
    ${test_name}${TEST_PARAM_DELIM}memtx
    ${test_name}${TEST_PARAM_DELIM}vinyl
    PROPERTIES DISABLED TRUE
  )
endforeach()
