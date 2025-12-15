# Use `lib` prefix for loading via FFI and `require()`.
AddTestLib(libctest libctest.c)
enable_language(CXX)
AddTestLib(libcpptest libcpptest.cpp)

add_custom_target(LuaJIT-tests-libs DEPENDS ${TESTLIBS})
