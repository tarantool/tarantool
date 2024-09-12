build_module(module_api module_api.c)
target_link_libraries(module_api msgpuck)

# gh-5313: verify that module.h actually conforms to the C99
# standard.
set(CMAKE_C_FLAGS "-Wall -Wextra -std=c99")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -Werror")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2")

tarantool_make_lua_path(LUA_PATH
  PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/?.lua
    ${CMAKE_CURRENT_SOURCE_DIR}/lua/?.lua
    # Needed for test-run/test_run.lua.
    ${PROJECT_SOURCE_DIR}/test-run/?.lua
)

tarantool_make_lua_path(LUA_CPATH
  PATHS
  ${CMAKE_CURRENT_BINARY_DIR}/?${CMAKE_SHARED_LIBRARY_SUFFIX}
)

set(TEST_SUITE_NAME "app-tap")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

# XXX: The call produces both test and target <app-tap-deps>
# as a side effect.
_add_test_suite_target(${TEST_SUITE_NAME}
  LABELS ${TEST_SUITE_NAME}
  DEPENDS module_api
)

list(APPEND TEST_ENV
  "LUA_PATH=${LUA_PATH}"
  "LUA_CPATH=${LUA_CPATH}"
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
  set(CTEST_FAIL_REGEXP_EFFECTIVE "")
  # "Error regular expression found in output. Regex=[bad plan]."
  if(NOT ${test_title} STREQUAL "test/app-tap/tap.test.lua")
    set(CTEST_FAIL_REGEXP_EFFECTIVE ${CTEST_FAIL_REGEXP})
  endif()
  set_tests_properties(${test_title} PROPERTIES
    ENVIRONMENT "${TEST_ENV};TT_WORK_DIR=${work_dir}"
    LABELS "${TEST_SUITE_NAME};integration"
    DEPENDS ${TEST_SUITE_NAME}-deps
    FAIL_REGULAR_EXPRESSION "${CTEST_FAIL_REGEXP_EFFECTIVE}"
  )
endforeach()

# See test/app-tap/suite.ini.
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  set_tests_properties(
    test/app-tap/gh-2717-no-quit-sigint.test.lua
    PROPERTIES DISABLED TRUE
  )
endif()

# See console.skipcond, disabled on FreeBSD due to fail #4271.
if(UNIX AND NOT APPLE)
  set_tests_properties(
    test/app-tap/console.test.lua
    PROPERTIES DISABLED TRUE
  )
endif()

# See fail_main.skipcond and popen.skipcond, disabled on OpenBSD.
if(OpenBSD)
  set_tests_properties(
    test/app-tap/fail_main.test.lua
    test/app-tap/popen.test.lua
    PROPERTIES DISABLED TRUE
  )
endif()

set_tests_properties(
  # REASON: "Inspector not started"
  test/app-tap/debug.test.lua
  test/app-tap/inspector.test.lua
  # REASON: Broken TT_WORK_DIR.
  test/app-tap/logger.test.lua
  PROPERTIES DISABLED TRUE
)
