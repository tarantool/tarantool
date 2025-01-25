set(TEST_SUITE_NAME "config-luatest")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

# XXX: The call produces both test and target <config-luatest-deps>
# as a side effect.
_add_test_suite_target(${TEST_SUITE_NAME}
  LABELS ${TEST_SUITE_NAME}
)

list(APPEND TEST_ENV
  "LUA_PATH=${LUATEST_LUA_PATH}"
  "SOURCEDIR=${PROJECT_SOURCE_DIR}"
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
                   --shuffle all:${RANDOM_SEED}
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
