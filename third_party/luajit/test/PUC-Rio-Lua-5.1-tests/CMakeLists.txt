# Test suite that has been added from PUC-Rio Lua 5.1 test archive
# in scope of https://github.com/tarantool/tarantool/issues/5845.

# XXX: There are two ways to set up the proper environment
# described in the suite's README:
# * set LUA_PATH to "?;./?.lua"
# * or, better yet, set LUA_PATH to "./?.lua;;" and LUA_INIT to
#   "package.path = '?;'..package.path"
# Unfortunately, Tarantool doesn't support LUA_INIT and most
# likely it never will. For more info, see
# https://github.com/tarantool/tarantool/issues/5744
# Hence, there is no way other than set LUA_PATH environment
# variable as proposed in the first case.
make_lua_path(LUA_PATH
  PATHS
    "?"
    ${CMAKE_CURRENT_SOURCE_DIR}/?.lua
)

# Establish PUC-Rio-Lua-5.1-tests-prepare target that contains
# rules for <libs/*> libraries compilation and creates <libs/P1>
# subdirectory.
add_subdirectory(libs)

# TODO: PUC-Rio Lua 5.1 test suite also has special header
# <ltests.h> and <ltests.c> translation unit to check some
# internal behaviour of the Lua implementation (see etc/
# directory). It modifies realloc function to check memory
# consistency and also contains tests for yield in hooks
# and for the Lua C API.
# But, unfortunately, <ltests.c> depends on specific PUC-Rio
# Lua 5.1 internal headers and should be adapted for LuaJIT.

set(TEST_SUITE_NAME "PUC-Rio-Lua-5.1-tests")

# XXX: The call produces both test and target
# <PUC-Rio-Lua-5.1-tests-deps> as a side effect.
add_test_suite_target(PUC-Rio-Lua-5.1-tests
  LABELS ${TEST_SUITE_NAME}
  DEPENDS ${LUAJIT_TEST_DEPS} PUC-Rio-Lua-5.1-tests-prepare
)

# The test suite has its own test runner
# (test/PUC-Rio-Lua-5.1-tests/all.lua), it is not possible
# to run these tests one-by-one by CTest.
set(test_title "test/${TEST_SUITE_NAME}")
add_test(NAME "${test_title}"
  COMMAND ${LUAJIT_TEST_COMMAND} ${CMAKE_CURRENT_SOURCE_DIR}/all.lua
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
set_tests_properties("${test_title}" PROPERTIES
  ENVIRONMENT "LUA_PATH=${LUA_PATH}"
  LABELS ${TEST_SUITE_NAME}
  DEPENDS PUC-Rio-Lua-5.1-tests-deps
)

# vim: expandtab tabstop=2 shiftwidth=2
