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
)

if (ENABLE_GCOV)
  list(APPEND LUA_TESTS_CMAKE_FLAGS "-DENABLE_COV=ON")
endif()

if (ENABLE_ASAN)
  list(APPEND LUA_TESTS_CMAKE_FLAGS "-DENABLE_ASAN=ON")
endif()

if (ENABLE_UB_SANITIZER)
  list(APPEND LUA_TESTS_CMAKE_FLAGS "-DENABLE_UBSAN=ON")
endif()

include(ExternalProject)

set(GIT_REF "854eba8e865ee1ac4c602fcb71836219c5d2f046")
# Git reference can be overridden with environment variable. It is
# needed for checking build by project itself.
if (DEFINED ENV{LUA_TESTS_GIT_REF})
  set(GIT_REF "$ENV{LUA_TESTS_GIT_REF}")
endif()

ExternalProject_Add(${LUA_TESTS_TARGET_NAME}
  PREFIX ${LUA_TESTS_TARGET_NAME}
  GIT_REPOSITORY https://github.com/ligurio/lunapark.git
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
# 1. https://github.com/ligurio/lunapark/blob/ef74f0558bb913edef85e7a774fada4ecc82247b/tests/capi/CMakeLists.txt#L46-L58
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
    LABELS "fuzzing;fuzzing-c;luajit-capi"
    DEPENDS ${LUA_TESTS_TARGET_NAME}
  )
endforeach()

# The same tests as in ligurio/lunapark, unsupported tests
# listed in LAPI_TESTS_UNSUPPORTED and disabled.
string(JOIN ";" LAPI_TESTS
  bitop_arshift_test.lua
  bitop_band_test.lua
  bitop_bnot_test.lua
  bitop_bor_test.lua
  bitop_bswap_test.lua
  bitop_bxor_test.lua
  bitop_lshift_test.lua
  bitop_rol_test.lua
  bitop_ror_test.lua
  bitop_rshift_test.lua
  bitop_tobit_test.lua
  bitop_tohex_test.lua
  builtin_assert_test.lua
  builtin_collectgarbage_test.lua
  builtin_concat_test.lua
  builtin_dofile_test.lua
  builtin_dostring_test.lua
  builtin_error_test.lua
  builtin_getfenv_test.lua
  builtin_getmetatable_test.lua
  builtin_ipairs_test.lua
  builtin_length_test.lua
  builtin_loadfile_test.lua
  builtin_loadstring_test.lua
  builtin_load_test.lua
  builtin_next_test.lua
  builtin_pairs_test.lua
  builtin_rawequal_test.lua
  builtin_rawget_test.lua
  builtin_rawset_test.lua
  builtin_select_test.lua
  builtin_setmetatable_test.lua
  builtin_tonumber_test.lua
  builtin_tostring_test.lua
  builtin_unpack_test.lua
  coroutine_torture_test.lua
  debug_torture_test.lua
  io_torture_test.lua
  jit_p_test.lua
  math_abs_test.lua
  math_acos_test.lua
  math_asin_test.lua
  math_atan_test.lua
  math_ceil_test.lua
  math_cos_test.lua
  math_deg_test.lua
  math_exp_test.lua
  math_floor_test.lua
  math_fmod_test.lua
  math_log_test.lua
  math_modf_test.lua
  math_pow_test.lua
  math_rad_test.lua
  math_randomseed_test.lua
  math_sin_test.lua
  math_sqrt_test.lua
  math_tan_test.lua
  math_tointeger_test.lua
  math_ult_test.lua
  os_date_test.lua
  os_difftime_test.lua
  os_getenv_test.lua
  os_setlocale_test.lua
  os_time_test.lua
  package_require_test.lua
  string_buffer_encode_test.lua
  string_buffer_torture_test.lua
  string_byte_test.lua
  string_char_test.lua
  string_dump_test.lua
  string_find_test.lua
  string_format_test.lua
  string_gmatch_test.lua
  string_gsub_test.lua
  string_len_test.lua
  string_lower_test.lua
  string_match_test.lua
  string_packsize_test.lua
  string_pack_test.lua
  string_rep_test.lua
  string_reverse_test.lua
  string_sub_test.lua
  string_unpack_test.lua
  string_upper_test.lua
  table_clear_test.lua
  table_concat_test.lua
  table_create_test.lua
  table_foreachi_test.lua
  table_foreach_test.lua
  table_insert_test.lua
  table_maxn_test.lua
  table_move_test.lua
  table_pack_test.lua
  table_remove_test.lua
  table_sort_test.lua
  utf8_char_test.lua
  utf8_codepoint_test.lua
  utf8_codes_test.lua
  utf8_len_test.lua
  utf8_offset_test.lua
)

string(JOIN ";" LAPI_TESTS_UNSUPPORTED
  math_tointeger_test.lua
  math_ult_test.lua
  string_buffer_encode_test.lua
  string_buffer_torture_test.lua
  string_packsize_test.lua
  string_pack_test.lua
  string_unpack_test.lua
  table_pack_test.lua
  utf8_char_test.lua
  utf8_codepoint_test.lua
  utf8_codes_test.lua
  utf8_len_test.lua
  utf8_offset_test.lua
)

make_lua_path(LAPI_LUA_PATH
  PATHS
  ${LUZER_LUA_PATH}
  ${CMAKE_CURRENT_SOURCE_DIR}/?.lua
)

list(APPEND LAPI_TEST_ENV
  "LUA_PATH=${LAPI_LUA_PATH};"
  "LUA_CPATH=${LUZER_LUA_CPATH};"
)
if (ENABLE_ASAN)
  list(APPEND TEST_ENV
    "LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/asan/lsan.supp;"
  )
endif()

foreach(test_name ${LAPI_TESTS})
  create_luzer_test(
    FILENAME ${LUA_TESTS_PREFIX}/src/tests/lapi/${test_name}
    TEST_ENV "${LAPI_TEST_ENV}"
    LABELS "fuzzing;fuzzing-lua;luajit-lapi"
  )
endforeach()

# Disable tests unsupported by LuaJIT.
foreach(test_name ${LAPI_TESTS_UNSUPPORTED})
  set(test_path ${LUA_TESTS_PREFIX}/src/tests/lapi/${test_name})
  string(REGEX REPLACE "^${PROJECT_SOURCE_DIR}/" "" test_title "${test_path}")
  set_tests_properties(${test_title} PROPERTIES
    DISABLED TRUE
  )
endforeach()

add_dependencies(${LUA_TESTS_TARGET_NAME} luzer-library)

unset(GIT_REF)
unset(CAPI_TESTS)
unset(LAPI_TESTS)
unset(LAPI_LUA_PATH)
unset(LUA_TESTS_CMAKE_FLAGS)
unset(LUA_TESTS_PREFIX)
unset(LUA_TESTS_TARGET_NAME)
