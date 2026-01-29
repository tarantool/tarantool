# cp: cannot stat 'build/test/fuzz/lua-tests/src/tests/lapi/lib.lua': No such file or directory

set(TEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/lua-tests/src/tests/lapi)
file(MAKE_DIRECTORY ${TEST_DIR})
file(WRITE ${TEST_DIR}/lib.lua "")
