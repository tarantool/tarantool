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
