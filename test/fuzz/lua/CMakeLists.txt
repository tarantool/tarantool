set(TARANTOOL_BIN $<TARGET_FILE:tarantool>)

set(LUA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/?.lua\;\;")

set(TEST_SUITE_NAME "fuzzing-lua")

message(STATUS "Add fuzzing tests (Lua)")
function(create_fuzz_lua_test)
  set(prefix FUZZ)
  set(noValues)
  set(singleValues NAME FLAVOR)
  set(multiValues DEPENDS OPTS)

  # FIXME: if we update to CMake >= 3.5, can remove this line.
  include(CMakeParseArguments)
  cmake_parse_arguments(${prefix}
                        "${noValues}"
                        "${singleValues}"
                        "${multiValues}"
                        ${ARGN})

  set(TEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/${FUZZ_NAME}.lua)
  set(TEST_DEPS tarantool ${FUZZ_DEPENDS} ${TEST_PATH})

  # XXX: The call produces both test and target
  # <${FUZZ_NAME}-${FUZZ_FLAVOR}-deps> as a side effect.
  _add_test_suite_target(${FUZZ_NAME}-${FUZZ_FLAVOR}
    LABELS ${TEST_SUITE_NAME}
    DEPENDS ${TEST_DEPS})
  set(test_title "test/fuzz/lua/${FUZZ_NAME}.lua")
  if (FUZZ_FLAVOR)
    set(test_title "${test_title}-${FUZZ_FLAVOR}")
  endif()
  add_test(NAME ${test_title}
    COMMAND ${TARANTOOL_BIN} ${CMAKE_CURRENT_SOURCE_DIR}/${FUZZ_NAME}.lua ${FUZZ_OPTS}
  )
  set_tests_properties(${test_title} PROPERTIES
    ENVIRONMENT "LUA_PATH=${LUA_PATH}"
    LABELS ${TEST_SUITE_NAME}
    DEPENDS ${FUZZ_NAME}-${FUZZ_FLAVOR}-deps)
endfunction()

set(TEST_ENGINE_DEFAULT_OPTS --test_duration=30 --workers=500 --verbose)
create_fuzz_lua_test(NAME test_engine
    FLAVOR vinyl
    OPTS --engine vinyl ${TEST_ENGINE_DEFAULT_OPTS}
         --test_dir ${CMAKE_CURRENT_BINARY_DIR}/test_engine_vinyl
)

create_fuzz_lua_test(NAME test_engine
    FLAVOR memtx
    OPTS --engine memtx ${TEST_ENGINE_DEFAULT_OPTS}
         --test_dir ${CMAKE_CURRENT_BINARY_DIR}/test_engine_memtx
)
