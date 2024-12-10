set(LUA_C_API_TESTS_TARGET lua-c-api-tests)
set(LUA_C_API_TESTS_SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/${LUA_C_API_TESTS_TARGET})
set(LUA_C_API_TESTS_BINARY_DIR ${PROJECT_BINARY_DIR}/build/${LUA_C_API_TESTS_TARGET}/work)
set(LUA_C_API_TESTS_INSTALL_DIR ${PROJECT_BINARY_DIR}/build/${LUA_C_API_TESTS_TARGET}/dest)

set(LUA_C_API_TESTS_REPO https://github.com/ligurio/lua-c-api-tests.git)

string(JOIN ";" LUA_C_API_TESTS
  ffi_cdef_proto_test
  lua_dump_test
  luaL_gsub_test
  luaL_loadbuffer_proto_test
  luaL_loadbufferx_test
  lua_load_test
  luaL_traceback_test
  torture_test
)

include (ExternalProject)
ExternalProject_Add(${LUA_C_API_TESTS_TARGET}
  PREFIX ${LUA_C_API_TESTS_TARGET}
  GIT_REPOSITORY ${LUA_C_API_TESTS_REPO}
  # FIXME: Depends on to be merged changes,
  # https://github.com/ligurio/lua-c-api-tests/pull/113.
  GIT_TAG ligurio/gh-xxxx-set-liblua-outside
  SOURCE_DIR ${LUA_C_API_TESTS_SOURCE_DIR}
  DOWNLOAD_DIR ${LUA_C_API_TESTS_BINARY_DIR}
  TMP_DIR ${LUA_C_API_TESTS_BINARY_DIR}/tmp
  STAMP_DIR ${LUA_C_API_TESTS_BINARY_DIR}/stamp
  BINARY_DIR ${LUA_C_API_TESTS_BINARY_DIR}/lua-c-api-tests
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -S <SOURCE_DIR>
        -B <BINARY_DIR>
        -G ${CMAKE_GENERATOR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
        # Propagates support of OSS Fuzz.
        -DOSS_FUZZ=${OSS_FUZZ}
        # Disable building Protobuf library, system library
        # is used.
        -DENABLE_BUILD_PROTOBUF=FALSE
        # Enable randomness in a register allocation.
        -DENABLE_LUAJIT_RANDOM_RA=TRUE
        -DIS_LUAJIT=TRUE
        -DLUA_INCLUDE_DIR=${PROJECT_SOURCE_DIR}/third_party/luajit/src/
        -DLUA_LIBRARIES=${LUAJIT_LIBRARIES}
        -DLUA_EXECUTABLE=${LUAJIT_TEST_BINARY}
        -DLUA_VERSION_STRING="tarantool"
  BUILD_COMMAND cd <BINARY_DIR> && ${CMAKE_MAKE_PROGRAM} "${LUA_C_API_TESTS}"
  INSTALL_COMMAND ""
  CMAKE_GENERATOR ${CMAKE_GENERATOR}
  BUILD_BYPRODUCTS "${LUA_C_API_TESTS}"
)

add_dependencies(${LUA_C_API_TESTS_TARGET} libluajit_static)
