build_module(check_merge_source check_merge_source.c)

set(TEST_SUITE_NAME "box-tap")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

tarantool_make_lua_path(LUA_PATH
  PATHS
    ${PROJECT_SOURCE_DIR}/test-run/?.lua
)

# XXX: The call produces both test and target <box-tap-deps>
# as a side effect.
_add_test_suite_target(${TEST_SUITE_NAME}
  LABELS ${TEST_SUITE_NAME}
  DEPENDS check_merge_source
)

list(APPEND TEST_ENV
  "BUILDDIR=${PROJECT_BINARY_DIR}"
  "LUA_PATH=${LUA_PATH}"
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
  set(test_title "test/${TEST_SUITE_NAME}/${test_name}")
  set(work_dir "${CMAKE_CURRENT_BINARY_DIR}/${test_name}-workdir")
  file(MAKE_DIRECTORY ${work_dir})
  set(TEST_CMD "${RM} -f ${work_dir}/* && ${TARANTOOL_BIN} ${test_path}")
  add_test(NAME ${test_title}
           COMMAND ${BASH} -c "${TEST_CMD}"
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_tests_properties(${test_title} PROPERTIES
    # LISTEN is required by these tests:
    # - test/box-tap/auth.test.lua
    # - test/box-tap/extended_error.test.lua
    # - test/box-tap/session.test.lua
    # XXX: Should it be located in a VARDIR?
    ENVIRONMENT "${TEST_ENV};TT_WORK_DIR=${work_dir};LISTEN=${work_dir}/listen.sock;"
    LABELS "${TEST_SUITE_NAME};integration"
    DEPENDS ${TEST_SUITE_NAME}-deps
    FAIL_REGULAR_EXPRESSION "${CTEST_FAIL_REGEXP}"
  )
endforeach()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  set_tests_properties(
    # See test/box-tap/suite.ini.
    test/box-tap/errinj_set_with_enviroment_vars.test.lua
    test/box-tap/gh-6198-max-cnt-of-tuple-fields-insert-overflow-err-msg.test.lua
    PROPERTIES DISABLED TRUE
  )
endif()

# XXX: Parameterize tests: test/box-tap/suite.cfg.

# See cfg.skipcond, disabled on OpenBSD.
if(OpenBSD)
  set_tests_properties(
    test/box-tap/cfg.test.lua
    PROPERTIES DISABLED TRUE
  )
endif()

set_tests_properties(
  # REASON: gh-5602-environment-cfg-test-cases.lua: No such file or directory
  test/box-tap/gh-5602-environment-vars-cfg.test.lua
  # REASON: "Inspector not started"
  test/box-tap/net.box.test.lua
  test/box-tap/session.test.lua
  test/box-tap/session.storage.test.lua
  # REASON: Broken TT_WORK_DIR.
  test/box-tap/gh-4562-errno-at-xdir_scan.test.lua
  test/box-tap/cfg.test.lua
  PROPERTIES DISABLED TRUE
)
