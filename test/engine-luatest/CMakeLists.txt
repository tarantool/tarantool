build_module(gh_6986 gh_6986_c_constraint_test.c)
target_link_libraries(gh_6986 msgpuck core)
build_module(gh_9965 gh_9965_non_compact_mp.c)
target_link_libraries(gh_9965 msgpuck)

tarantool_make_lua_path(LUA_CPATH
  PATHS
  ${CMAKE_CURRENT_BINARY_DIR}/?${CMAKE_SHARED_LIBRARY_SUFFIX}
)

set(TEST_SUITE_NAME "engine-luatest")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

# XXX: The call produces both test and target <engine-luatest-deps>
# as a side effect.
_add_test_suite_target(${TEST_SUITE_NAME}
  LABELS "${TEST_SUITE_NAME};regression;"
  DEPENDS gh_6986 gh_9965
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
