add_subdirectory(src)

make_lua_path(LUA_CPATH
  PATHS
  ${CMAKE_CURRENT_BINARY_DIR}/src/?${CMAKE_SHARED_LIBRARY_SUFFIX}
)

set(LUAJIT_TESTS_ENV
  "LUA_CPATH=${LUA_CPATH}"
)

set(LD_LIBRARY_PATH "${CMAKE_CURRENT_BINARY_DIR}/src/:")

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  list(APPEND LUAJIT_TESTS_ENV DYLD_LIBRARY_PATH="${LD_LIBRARY_PATH}")
else()
  list(APPEND LUAJIT_TESTS_ENV LD_LIBRARY_PATH="${LD_LIBRARY_PATH}")
endif()

if(LUAJIT_USE_ASAN)
  # When running LuaJIT-tests under ASAN, the internal ASAN check
  # failed:
  # AddressSanitizer: CHECK failed: asan_interceptors.cpp:356
  # "((__interception::real___cxa_throw)) != (0)" (0x0, 0x0)
  # This is a workaround suggested at
  # https://github.com/google/sanitizers/issues/934.
  LibRealPath(LIB_STDCPP libstdc++.so)
  # XXX: GCC requires both. Clang requires only libstdc++.
  # Be aware, ASAN runtime in Clang on Linux was offered only in
  # the form of a library with differ name from libasan.so (e.g.
  # libclang_rt.asan-x86_64.a).
  # See also:
  # https://github.com/google/sanitizers/wiki/AddressSanitizerAsDso.
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    LibRealPath(LIB_ASAN libasan.so)
    # XXX: The items of the list in LD_PRELOAD can be separated
    # by spaces or colons, and there is no support for escaping
    # either separator, see ld.so(8).
    list(APPEND LUAJIT_TESTS_ENV LD_PRELOAD=${LIB_ASAN}:${LIB_STDCPP})
  else()
    list(APPEND LUAJIT_TESTS_ENV LD_PRELOAD=${LIB_STDCPP})
  endif()
endif()

if(NOT LUAJIT_DISABLE_JIT)
  list(APPEND LUAJIT_TEST_TAGS_EXTRA +jit)
endif()

if(LUAJIT_NO_UNWIND)
  # Test <catch_cpp.lua> verifies the interoperability with C++
  # ABI exceptions, so it requires external unwinding enabled.
  # Hence, skip them otherwise.
  list(APPEND LUAJIT_TEST_TAGS_EXTRA +internal_unwinder)
endif()

if(CMAKE_C_FLAGS MATCHES "-march=skylake-avx512")
  # FIXME: Test <bit64.lua> verifies bitwise operations on
  # numbers. There is a known issue - bitop doesn't work in LuaJIT
  # built with the enabled AVX512 instruction set, see
  # https://github.com/tarantool/tarantool/issues/6787.
  # Hence, skip this when "skylake-avx512" is passed.
  list(APPEND LUAJIT_TEST_TAGS_EXTRA +avx512)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  GetLinuxDistro(LINUX_DISTRO)
  # Alpine uses musl instead of glibc.
  if(NOT LINUX_DISTRO MATCHES "alpine")
    GetLibCVersion(LIBC_VERSION)
    # XXX: <tonumber_scan.lua> uses `strtod()`, old versions of
    # which have the bug [1] for "0x1p-2075" parsing. Add the skip
    # check for it.
    # [1]: https://sourceware.org/bugzilla/show_bug.cgi?id=16151
    list(APPEND LUAJIT_TEST_TAGS_EXTRA +libc=${LIBC_VERSION})
  endif()
endif()

if(LUAJIT_ENABLE_TABLE_BUMP)
  # Test <string/dump.lua> verifies that the bytecode is unchanged
  # for the prototype with the recorded trace. Table bump
  # optimization changes the TNEW/TDUP bytecodes, so skip the test
  # in that case.
  list(APPEND LUAJIT_TEST_TAGS_EXTRA +table_bump)
endif()

set(TEST_SUITE_NAME "LuaJIT-tests")

# XXX: The call produces both test and target <LuaJIT-tests-deps>
# as a side effect.
add_test_suite_target(LuaJIT-tests
  LABELS ${TEST_SUITE_NAME}
  DEPENDS ${LUAJIT_TEST_DEPS} LuaJIT-tests-libs
)

# The test suite has its own test runner
# (test/LuaJIT-tests/test.lua), it is not possible to run these
# tests one-by-one by CTest.
set(test_title "test/${TEST_SUITE_NAME}")
add_test(NAME "${test_title}"
  COMMAND ${LUAJIT_TEST_COMMAND} ${CMAKE_CURRENT_SOURCE_DIR}/test.lua
                                 +slow +ffi +bit ${LUAJIT_TEST_TAGS_EXTRA}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties("${test_title}" PROPERTIES
  LABELS ${TEST_SUITE_NAME}
  ENVIRONMENT "${LUAJIT_TESTS_ENV}"
  DEPENDS LuaJIT-tests-deps
)
