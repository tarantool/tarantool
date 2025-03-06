tarantool_make_lua_path(LUA_PATH
  PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/?.lua
)

set(TEST_SUITE_NAME "engine-tap")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

# XXX: The call produces both test and target <engine-tap-deps>
# as a side effect.
_add_test_suite_target(${TEST_SUITE_NAME}
  LABELS ${TEST_SUITE_NAME}
)

list(APPEND TEST_ENV
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
  set(test_title "test/${TEST_SUITE_NAME}/${test_name}${TEST_PARAM_DELIM}memtx")
  set(work_dir "${CMAKE_CURRENT_BINARY_DIR}/${test_name}-workdir-memtx")
  file(MAKE_DIRECTORY ${work_dir})
  set(TEST_CMD "${RM} -rf ${work_dir}/* && ${TARANTOOL_BIN} ${test_path}")
  add_test(NAME ${test_title}
           COMMAND ${BASH} -c "${TEST_CMD}"
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_tests_properties(${test_title} PROPERTIES
    ENVIRONMENT "${TEST_ENV};TT_WORK_DIR=${work_dir};TEST_ENGINE=memtx"
    LABELS "${TEST_SUITE_NAME};regression"
    DEPENDS ${TEST_SUITE_NAME}-deps
    FAIL_REGULAR_EXPRESSION "${CTEST_FAIL_REGEXP}"
  )

  set(test_title "test/${TEST_SUITE_NAME}/${test_name}${TEST_PARAM_DELIM}vinyl")
  set(work_dir "${CMAKE_CURRENT_BINARY_DIR}/${test_name}-workdir-vinyl")
  file(MAKE_DIRECTORY ${work_dir})
  set(TEST_CMD "${RM} -rf ${work_dir}/* && ${TARANTOOL_BIN} ${TAP_TEST_FLAGS} ${test_path}")
  add_test(NAME ${test_title}
           COMMAND ${BASH} -c "${TEST_CMD}"
           WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )
  set_tests_properties(${test_title} PROPERTIES
    ENVIRONMENT "${TEST_ENV};TT_WORK_DIR=${work_dir};TEST_ENGINE=vinyl;"
    LABELS "${TEST_SUITE_NAME};regression"
    DEPENDS ${TEST_SUITE_NAME}-deps
    FAIL_REGULAR_EXPRESSION "${CTEST_FAIL_REGEXP}"
  )
endforeach()
