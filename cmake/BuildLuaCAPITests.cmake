set(LUA_C_API_TESTS_TARGET lua-c-api-tests)
# Beware: OSS Fuzz build script searches tests in a directory
# `${PROJECT_BINARY_DIR}/test/fuzz`.
set(LUA_C_API_TESTS_PREFIX ${PROJECT_BINARY_DIR}/test/fuzz/${LUA_C_API_TESTS_TARGET})

# The test `luaL_loadbuffer_proto_test` is not enabled, because
# it is quite similar to `luaL_loadbuffer_fuzzer` located in
# test/fuzz/luaL_loadbuffer/.
string(JOIN ";" LUA_C_API_TESTS
  ffi_cdef_proto_test
  lua_dump_test
  luaL_gsub_test
  luaL_loadbufferx_test
  lua_load_test
  luaL_traceback_test
  torture_test
)

list(APPEND LUA_C_API_TESTS_CMAKE_FLAGS
  "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
  "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
  "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
  # Propagates support of OSS Fuzz.
  "-DOSS_FUZZ=${OSS_FUZZ}"
  # Disable building Protobuf library, system library
  # is used.
  "-DENABLE_BUILD_PROTOBUF=FALSE"
  "-DIS_LUAJIT=TRUE"
  "-DLUA_INCLUDE_DIR=${PROJECT_SOURCE_DIR}/third_party/luajit/src/"
  "-DLUA_LIBRARIES=${LUAJIT_LIBRARIES}"
  "-DLUA_EXECUTABLE=${LUAJIT_TEST_BINARY}"
  "-DLUA_VERSION_STRING=\"tarantool\""
)

if (ENABLE_ASAN)
  list(APPEND LUA_C_API_TESTS_CMAKE_FLAGS "-DENABLE_ASAN=ON")
endif()

if (ENABLE_UB_SANITIZER)
  list(APPEND LUA_C_API_TESTS_CMAKE_FLAGS "-DENABLE_UBSAN=ON")
endif()

include(ExternalProject)

set(LUA_C_API_TESTS_GIT_TAG "master")
# Git tag can be set using environment variable. It is needed for
# building tests provided by this CMake module by project itself.
if (DEFINED ENV{LUA_C_API_TESTS_GIT_TAG})
  set(LUA_C_API_TESTS_GIT_TAG "$ENV{LUA_C_API_TESTS_GIT_TAG}")
endif()

ExternalProject_Add(${LUA_C_API_TESTS_TARGET}
  PREFIX ${LUA_C_API_TESTS_TARGET}
  GIT_REPOSITORY https://github.com/ligurio/lua-c-api-tests.git
  # FIXME: Depends on to be merged changes,
  # https://github.com/ligurio/lua-c-api-tests/pull/113.
  GIT_TAG ligurio/gh-xxxx-set-liblua-outside
  SOURCE_DIR ${LUA_C_API_TESTS_PREFIX}/src
  BINARY_DIR ${LUA_C_API_TESTS_PREFIX}/build
  TMP_DIR ${LUA_C_API_TESTS_PREFIX}/tmp
  STAMP_DIR ${LUA_C_API_TESTS_PREFIX}/stamp
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -S <SOURCE_DIR>
        -B <BINARY_DIR>
        -G ${CMAKE_GENERATOR}
        "${LUA_C_API_TESTS_CMAKE_FLAGS}"
  BUILD_COMMAND cd <BINARY_DIR> && ${CMAKE_MAKE_PROGRAM} "${LUA_C_API_TESTS}"
  INSTALL_COMMAND ""
  CMAKE_GENERATOR ${CMAKE_GENERATOR}
  BUILD_BYPRODUCTS "${LUA_C_API_TESTS}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

add_dependencies(${LUA_C_API_TESTS_TARGET} libluajit_static)

# Running CTest tests from an ExternalProject is done by running
# `ctest` in a build directory used by ExternalProject.
# Used LibFuzzer options are similar [1] to options used by
# Tarantool and these options cannot be overridden. By default
# number of runs is equal to 5 and can be overridden with
# environment variable `RUNS`.
#
# 1. https://github.com/ligurio/lua-c-api-tests/blob/ef74f0558bb913edef85e7a774fada4ecc82247b/tests/capi/CMakeLists.txt#L46-L58
foreach(test ${LUA_C_API_TESTS})
  set(test_title test/fuzz/${test})
  add_test(NAME ${test_title}
    COMMAND ${CMAKE_CTEST_COMMAND}
      --test-dir "${LUA_C_API_TESTS_PREFIX}/build"
      --verbose
      --no-tests=error
      -R ${test}
  )
  set_tests_properties(${test_title} PROPERTIES
    LABELS "fuzzing;"
    DEPENDS lua-c-api-tests
  )
endforeach()

unset(LUA_C_API_TESTS_CMAKE_FLAGS)
unset(LUA_C_API_TESTS_PREFIX)
unset(LUA_C_API_TESTS_TARGET)
