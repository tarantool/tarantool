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
  set(TEST_TITLE "test/${TEST_SUITE_NAME}/${test_name}")
  set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/${test_name}-workdir")
  file(MAKE_DIRECTORY ${WORK_DIR})
  set(TEST_CMD "${RM} -rf ${WORK_DIR}/* && ${TARANTOOL_BIN} ${test_path}")
  add_test(NAME ${TEST_TITLE}
           COMMAND ${BASH} -c "${TEST_CMD}"
           WORKING_DIRECTORY ${WORK_DIR}
  )
  set(TEST_ENV_VARS "")
  if(NOT ${TEST_TITLE} STREQUAL test/app-tap/logger.test.lua)
    list(APPEND TEST_ENV_VARS TT_WORK_DIR=${WORK_DIR})
  endif()
  set_tests_properties(${TEST_TITLE} PROPERTIES
    ENVIRONMENT "${TEST_ENV};${TEST_ENV_VARS}"
    LABELS "${TEST_SUITE_NAME};regression"
    DEPENDS ${TEST_SUITE_NAME}-deps
    FAIL_REGULAR_EXPRESSION "${CTEST_FAIL_REGEXP}"
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
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
  set_tests_properties(
    test/app-tap/console.test.lua
    PROPERTIES DISABLED TRUE
  )
endif()

# See fail_main.skipcond and popen.skipcond, disabled on OpenBSD.
if(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
  set_tests_properties(
    test/app-tap/fail_main.test.lua
    test/app-tap/popen.test.lua
    PROPERTIES DISABLED TRUE
  )
endif()
