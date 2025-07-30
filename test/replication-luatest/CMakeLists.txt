set(TEST_SUITE_NAME "replication-luatest")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

# XXX: The call produces both test and target
# <replication-luatest-deps> as a side effect.
_add_test_suite_target(${TEST_SUITE_NAME}
  LABELS "${TEST_SUITE_NAME};regression;"
)

list(APPEND TEST_ENV
  "LUA_PATH=${LUATEST_LUA_PATH}"
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
  add_luatest_test(${TEST_SUITE_NAME} ${test_name} "${TEST_ENV}" FALSE)
endforeach()

# See test/replication-luatest/suite.ini.
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  set_tests_properties(
    test/replication-luatest/gh_5295_split_brain_test.lua
    test/replication-luatest/gh_6033_box_promote_demote_test.lua
    test/replication-luatest/gh_6036_qsync_order_test.lua
    test/replication-luatest/gh_6842_qsync_applier_order_test.lua
    test/replication-luatest/gh_7086_box_issue_promote_assert_test.lua
    test/replication-luatest/gh_7253_election_long_wal_write_test.lua
    PROPERTIES DISABLED TRUE
  )
endif()
