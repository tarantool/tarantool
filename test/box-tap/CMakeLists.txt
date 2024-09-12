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
  set(TEST_TITLE "test/${TEST_SUITE_NAME}/${test_name}")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/${test_name}-workdir")
  file(MAKE_DIRECTORY ${WORK_DIR})
  set(TEST_CMD "${RM} -rf ${WORK_DIR}/* && ${TARANTOOL_BIN} ${test_path}")
  add_test(NAME ${TEST_TITLE}
           COMMAND ${BASH} -c "${TEST_CMD}"
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  # `LISTEN` is required by the following tests:
  # - test/box-tap/auth.test.lua
  # - test/box-tap/extended_error.test.lua
  set(TEST_ENV_VARS "${TEST_ENV};LISTEN=${WORK_DIR}/listen.sock;")
  # The tests below could be broken by setting environment
  # variable `TT_WORK_DIR`, so `TT_WORK_DIR` is not set.
  if(NOT ${TEST_TITLE} STREQUAL test/box-tap/cfg.test.lua AND
     NOT ${TEST_TITLE} STREQUAL test/box-tap/gh-4562-errno-at-xdir_scan.test.lua)
    set(TEST_ENV_VARS ${TEST_ENV_VARS};TT_WORK_DIR=${WORK_DIR};)
  endif()
  set_tests_properties(${TEST_TITLE} PROPERTIES
    ENVIRONMENT "${TEST_ENV_VARS}"
    LABELS "${TEST_SUITE_NAME};regression"
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

# See cfg.skipcond, disabled on OpenBSD.
if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
  set_tests_properties(
    test/box-tap/cfg.test.lua
    PROPERTIES DISABLED TRUE
  )
endif()
