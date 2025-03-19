set(LUA_TESTS_TARGET_NAME lua-tests)
# Beware: OSS Fuzz build script searches tests in a directory
# `${PROJECT_BINARY_DIR}/test/fuzz`.
set(LUA_TESTS_PREFIX
    ${PROJECT_BINARY_DIR}/test/fuzz/${LUA_TESTS_TARGET_NAME})

# The test `luaL_loadbuffer_proto_test` is not enabled, because
# it is quite similar to `luaL_loadbuffer_fuzzer` located in
# test/fuzz/luaL_loadbuffer/.
string(JOIN ";" CAPI_TESTS
  ffi_cdef_proto_test
  lua_dump_test
  luaL_gsub_test
  luaL_loadbufferx_test
  lua_load_test
  luaL_traceback_test
  torture_test
)

string(JOIN ";" LAPI_TESTS
  bitop_arshift_test
  bitop_band_test
  bitop_bnot_test
  bitop_bor_test
  bitop_bswap_test
  bitop_bxor_test
  bitop_lshift_test
  bitop_rol_test
  bitop_ror_test
  bitop_rshift_test
  bitop_tobit_test
  bitop_tohex_test
)

list(APPEND LUA_TESTS_CMAKE_FLAGS
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
  "-DENABLE_LAPI_TESTS=ON"
)

if (ENABLE_ASAN)
  list(APPEND LUA_TESTS_CMAKE_FLAGS "-DENABLE_ASAN=ON")
endif()

if (ENABLE_UB_SANITIZER)
  list(APPEND LUA_TESTS_CMAKE_FLAGS "-DENABLE_UBSAN=ON")
endif()

include(ExternalProject)

set(GIT_REF "d4b91f6ac41ca28d19a4c7a44b85cd3c82f85479")
# Git reference can be overridden with environment variable. It is
# needed for checking build by project itself.
if (DEFINED ENV{LUA_TESTS_GIT_REF})
  set(GIT_REF "$ENV{LUA_TESTS_GIT_REF}")
endif()

ExternalProject_Add(${LUA_TESTS_TARGET_NAME}
  PREFIX ${LUA_TESTS_TARGET_NAME}
  GIT_REPOSITORY https://github.com/ligurio/lua-c-api-tests.git
  GIT_TAG ${GIT_REF}
  SOURCE_DIR ${LUA_TESTS_PREFIX}/src
  BINARY_DIR ${LUA_TESTS_PREFIX}/build
  TMP_DIR ${LUA_TESTS_PREFIX}/tmp
  STAMP_DIR ${LUA_TESTS_PREFIX}/stamp
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND ${CMAKE_COMMAND}
        -S <SOURCE_DIR>
        -B <BINARY_DIR>
        -G ${CMAKE_GENERATOR}
        "${LUA_TESTS_CMAKE_FLAGS}"
  BUILD_COMMAND cd <BINARY_DIR> && ${CMAKE_MAKE_PROGRAM} "${CAPI_TESTS}"
  INSTALL_COMMAND ""
  CMAKE_GENERATOR ${CMAKE_GENERATOR}
  BUILD_BYPRODUCTS "${CAPI_TESTS}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

add_dependencies(${LUA_TESTS_TARGET_NAME} libluajit_static)

# Running CTest tests from an ExternalProject is done by running
# `ctest` in a build directory used by ExternalProject.
# Used LibFuzzer options are similar [1] to options used by
# Tarantool and these options cannot be overridden. By default
# number of runs is equal to 5 and can be overridden with
# environment variable `RUNS`.
#
# 1. https://github.com/ligurio/lua-c-api-tests/blob/ef74f0558bb913edef85e7a774fada4ecc82247b/tests/capi/CMakeLists.txt#L46-L58
foreach(test ${CAPI_TESTS})
  set(test_title test/fuzz/${test})
  add_test(NAME ${test_title}
    COMMAND ${CMAKE_CTEST_COMMAND}
      --test-dir "${LUA_TESTS_PREFIX}/build"
      --verbose
      --no-tests=error
      -R ${test}
  )
  set_tests_properties(${test_title} PROPERTIES
    LABELS "fuzzing;"
    DEPENDS lua-tests
  )
endforeach()

set(DEFAULT_RUNS_NUMBER 5)

string(JOIN " " LIBFUZZER_OPTS
  -print_final_stats=1
  -print_pcs=1
  -reduce_inputs=1
  -reload=1
  -report_slow_units=5
  # Shell parameter expansion,
  # https://www.gnu.org/software/bash/manual/html_node/Shell-Parameter-Expansion.html
  -runs=\${RUNS:-${DEFAULT_RUNS_NUMBER}}
)

make_lua_path(LUA_CPATH
  PATHS
  ${PROJECT_BINARY_DIR}/luzer/build/luzer/?.so
)

make_lua_path(LUA_PATH
  PATHS
  ${PROJECT_BINARY_DIR}/luzer/?/init.lua
  ${CMAKE_CURRENT_SOURCE_DIR}/?.lua
)

list(APPEND LAPI_TEST_ENV
  "LUA_PATH=${LUA_PATH};"
  "LUA_CPATH=${LUA_CPATH};"
  "ASAN_OPTIONS=detect_odr_violation=0;"
  "LD_DYNAMIC_WEAK=1"
)

function(create_lapi_test)
  cmake_parse_arguments(
    FUZZ
    ""
    "FILENAME"
    ""
    ${ARGN}
  )
  get_filename_component(test_name ${FUZZ_FILENAME} NAME_WE)
  string(REPLACE "_test" "" test_prefix ${test_name})
  set(dict_path ${PROJECT_SOURCE_DIR}/corpus/${test_prefix}.dict)
  set(corpus_path ${PROJECT_SOURCE_DIR}/corpus/${test_prefix})
  if (EXISTS ${dict_path})
    set(LIBFUZZER_OPTS "${LIBFUZZER_OPTS} -dict=${dict_path}")
  endif ()
  if (EXISTS ${corpus_path})
    set(LIBFUZZER_OPTS "${LIBFUZZER_OPTS} ${corpus_path}")
  endif ()

  add_test(NAME ${test_name}
    COMMAND ${BASH} -c "${LUAJIT_TEST_BINARY} ${FUZZ_FILENAME}
                        ${LIBFUZZER_OPTS}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
  set_tests_properties(${test_name} PROPERTIES
    LABELS "lapi"
    ENVIRONMENT "${LAPI_TEST_ENV}"
    DEPENDS ${LUAJIT_TEST_BINARY}
  )
endfunction()

foreach(test_name ${LAPI_TESTS})
  create_lapi_test(FILENAME ${LUA_TESTS_PREFIX}/src/tests/lapi/${test_name}.lua)
endforeach()

unset(GIT_REF)
unset(CAPI_TESTS)
unset(LAPI_TESTS)
unset(LUA_TESTS_CMAKE_FLAGS)
unset(LUA_TESTS_PREFIX)
unset(LUA_TESTS_TARGET_NAME)
