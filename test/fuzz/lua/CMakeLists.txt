set(LUA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/?.lua\;\;")

set(TEST_SUITE_NAME "fuzzing-lua")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

# XXX: The call produces both test and target
# <${TEST_SUITE_NAME}-deps> as a side effect.
_add_test_suite_target(${TEST_SUITE_NAME}
  LABELS "${TEST_SUITE_NAME};fuzzing;"
  DEPENDS tarantool
)

set(test_path "test/fuzz/lua")
list(APPEND TEST_ENGINE_FLAGS
  --test_duration 60
  --workers 500
  --seed ${RANDOM_SEED}
  --verbose
)
add_test(NAME ${test_path}/test_engine.lua${TEST_PARAM_DELIM}memtx
  COMMAND ${TARANTOOL_BIN}
          ${CMAKE_CURRENT_SOURCE_DIR}/test_engine.lua
          ${TEST_ENGINE_FLAGS}
          --test_dir ${CMAKE_CURRENT_BINARY_DIR}/test_engine-memtx
          --engine memtx
)
add_test(NAME ${test_path}/test_engine.lua${TEST_PARAM_DELIM}vinyl
  COMMAND ${TARANTOOL_BIN}
          ${CMAKE_CURRENT_SOURCE_DIR}/test_engine.lua
          ${TEST_ENGINE_FLAGS}
          --test_dir ${CMAKE_CURRENT_BINARY_DIR}/test_engine-vinyl
          --engine vinyl
)
list(APPEND TEST_MVCC_FLAGS
  --test_dir ${CMAKE_CURRENT_BINARY_DIR}/test_mvcc
  --seed ${RANDOM_SEED}
)
add_test(NAME ${test_path}/test_mvcc.lua
  COMMAND ${TARANTOOL_BIN}
          ${CMAKE_CURRENT_SOURCE_DIR}/test_mvcc.lua
          ${TEST_MVCC_FLAGS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(
  ${test_path}/test_engine.lua${TEST_PARAM_DELIM}vinyl
  ${test_path}/test_engine.lua${TEST_PARAM_DELIM}memtx
  ${test_path}/test_mvcc.lua
  PROPERTIES
    ENVIRONMENT "LUA_PATH=${LUATEST_LUA_PATH};${LUA_PATH}"
    LABELS "${TEST_SUITE_NAME};fuzzing;"
    DEPENDS ${TEST_SUITE_NAME}-deps
)

LIST(APPEND BROKEN_LUZER_TESTS
  # To be enabled in scope tarantool/tarantool#11897.
  box_execute_test.lua
  # To be enabled in scope tarantool/tarantool#11898.
  json_decode_test.lua
  # To be enabled in scope tarantool/security#151.
  uri_parse_test.lua
  # To be enabled in scope tarantool/tarantool#12063.
  pickle_unpack_test.lua
  # To be enabled in scope tarantool/security#153.
  console_eval_test.lua
)

if(OSS_FUZZ)
  set(LUZER_TESTS_DIR ${CMAKE_BINARY_DIR}/luzer_tests)
  file(MAKE_DIRECTORY ${LUZER_TESTS_DIR})
endif()
set(LUZER_TESTS_OSS_FUZZ "")

function(create_luzer_test)
  cmake_parse_arguments(
    FUZZ
    ""
    "FILENAME;TEST_TITLE;DISABLED"
    "TEST_ENV;LABELS"
    ""
    ${ARGN}
  )
  get_filename_component(test_name ${FUZZ_FILENAME} NAME_WE)
  string(REPLACE "_test" "" test_prefix ${test_name})
  set(dict_path ${PROJECT_SOURCE_DIR}/test/static/corpus/${test_prefix}.dict)
  set(libfuzzer_opts_effective ${LIBFUZZER_OPTS})
  if(NOT DEFINED FUZZ_DISABLED)
    set(FUZZ_DISABLED FALSE)
  endif()
  if(NOT FUZZ_DISABLED)
    set(LUZER_TESTS_OSS_FUZZ ${LUZER_TESTS_OSS_FUZZ} ${FUZZ_FILENAME} PARENT_SCOPE)
  endif()
  if(EXISTS ${dict_path})
    set(libfuzzer_opts_effective "${libfuzzer_opts_effective} -dict=${dict_path}")
  endif()
  set(corpus_path ${PROJECT_SOURCE_DIR}/test/static/corpus/${test_prefix})
  if(EXISTS ${corpus_path})
    set(libfuzzer_opts_effective "${libfuzzer_opts_effective} ${corpus_path}")
  endif()

  set(test_title ${FUZZ_FILENAME})
  if (NOT ${FUZZ_TEST_TITLE})
    set(test_title ${FUZZ_TEST_TITLE})
  endif()
  string(REGEX REPLACE "^${PROJECT_SOURCE_DIR}/" "" test_title
    "${test_title}")
  add_test(NAME ${test_title}
    # LUAJIT_TEST_BINARY is used below because we want to be sure
    # that LuaJIT-based fuzzing tests are still working for the
    # Tarantool runtime.
    COMMAND ${BASH} -c "${LUAJIT_TEST_BINARY} \
      ${FUZZ_FILENAME} ${libfuzzer_opts_effective}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
  set_tests_properties(${test_title} PROPERTIES
    LABELS "${FUZZ_LABELS}"
    ENVIRONMENT "${FUZZ_TEST_ENV}"
    DEPENDS ${LUAJIT_TEST_BINARY}
    DISABLED ${FUZZ_DISABLED}
  )
endfunction()

# Build library and set LUZER_LUA_PATH and LUZER_LUA_CPATH.
include(BuildLuzer)

# Requires LUZER_LUA_CPATH and LUZER_LUA_PATH.
include(BuildLuaTests)

# test_engine.lua supports luzer, but test_mvcc.lua is not.
string(JOIN ";" LUZER_TESTS
  box_execute_test.lua
  console_eval_test.lua
  csv_load_test.lua
  decimal_new_test.lua
  json_decode_test.lua
  misc_memprof_test.lua
  misc_sysprof_test.lua
  msgpack_decode_test.lua
  msgpack_itv_test.lua
  net_box_call_test.lua
  pickle_unpack_test.lua
  uri_parse_test.lua
  uuid_frombin_test.lua
  yaml_decode_test.lua
)

make_lua_path(FUZZ_LUA_PATH
  PATHS
  ${LUZER_LUA_PATH}
  ${CMAKE_CURRENT_SOURCE_DIR}/?.lua
)
list(APPEND TEST_ENV
  "LUA_PATH=${FUZZ_LUA_PATH};"
  "LUA_CPATH=${LUZER_LUA_CPATH};"
)
if (ENABLE_ASAN)
  list(APPEND TEST_ENV
    "LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/asan/lsan.supp;"
  )
endif()
foreach(test_name ${LUZER_TESTS})
  set(TEST_IS_DISABLED FALSE)
  if(${test_name} IN_LIST BROKEN_LUZER_TESTS)
    set(TEST_IS_DISABLED TRUE)
  endif()
  message(STATUS "create_luzer_test APP TEST ${test_name}")
  create_luzer_test(
    FILENAME ${CMAKE_CURRENT_SOURCE_DIR}/${test_name}
    TEST_ENV "${TEST_ENV}"
    LABELS "fuzzing;${TEST_SUITE_NAME}"
    DISABLED ${TEST_IS_DISABLED}
  )
endforeach()

set(test_title
  "test/fuzz/lua/test_engine.lua${TEST_PARAM_DELIM}memtx${TEST_PARAM_DELIM}luzer")
create_luzer_test(
  FILENAME ${CMAKE_CURRENT_SOURCE_DIR}/test_engine.lua
  TEST_TITLE ${test_title}
  TEST_ENV "${TEST_ENV};TEST_ENGINE=memtx"
  LABELS "fuzzing;${TEST_SUITE_NAME}"
)
set(test_title
  "test/fuzz/lua/test_engine.lua${TEST_PARAM_DELIM}vinyl${TEST_PARAM_DELIM}luzer")
create_luzer_test(
  FILENAME ${CMAKE_CURRENT_SOURCE_DIR}/test_engine.lua
  TEST_TITLE ${test_title}
  TEST_ENV "${TEST_ENV};TEST_ENGINE=vinyl"
  LABELS "fuzzing;${TEST_SUITE_NAME}"
)

# Copying luzer-based tests located in different places to
# a single directory. Needed for OSS Fuzz. The target must be
# created once the tests created.
add_custom_target(copy_tests
  COMMAND ${CMAKE_COMMAND} -E copy
          ${LUZER_TESTS_OSS_FUZZ}
          ${LUZER_TESTS_DIR}
  COMMENT "Copying luzer-based tests to a single directory"
  DEPENDS lua-tests
)
