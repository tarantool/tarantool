# Test suite that has been added from lua-Harness test suite
# in scope of https://github.com/tarantool/tarantool/issues/5844.

# Tests create temporary files (see 303-package.t and 411-luajit.t
# for example) to be required. Also, they require some files from
# the original test source directory.
make_lua_path(LUA_PATH
  PATHS
    ./?.lua
    ${CMAKE_CURRENT_SOURCE_DIR}/?.lua
    ${LUAJIT_SOURCE_DIR}/?.lua
    ${LUAJIT_BINARY_DIR}/?.lua
)

set(TEST_SUITE_NAME "lua-Harness-tests")

# XXX: The call produces both test and target
# <lua-Harness-tests-deps> as a side effect.
add_test_suite_target(lua-Harness-tests
  LABELS ${TEST_SUITE_NAME}
  DEPENDS ${LUAJIT_TEST_DEPS}
)

set(LUA_TEST_SUFFIX .t)

file(GLOB tests ${CMAKE_CURRENT_SOURCE_DIR} "*${LUA_TEST_SUFFIX}")
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
    COMMAND ${LUAJIT_TEST_COMMAND} -l profile_luajit21 ${test_path}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
  set_tests_properties(${test_title} PROPERTIES
    ENVIRONMENT "LUA_PATH=${LUA_PATH}"
    LABELS ${TEST_SUITE_NAME}
    DEPENDS lua-Harness-tests-deps
  )
endforeach()

# vim: expandtab tabstop=2 shiftwidth=2
