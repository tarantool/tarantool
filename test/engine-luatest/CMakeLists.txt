build_module(gh_6986 gh_6986_c_constraint_test.c)
target_link_libraries(gh_6986 msgpuck core)
build_module(gh_9965 gh_9965_non_compact_mp.c)
target_link_libraries(gh_9965 msgpuck)

tarantool_make_lua_path(LUA_CPATH
  PATHS
  ${CMAKE_CURRENT_BINARY_DIR}/?${CMAKE_SHARED_LIBRARY_SUFFIX}
)

set(TEST_SUITE_NAME "engine-luatest")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

# XXX: The call produces both test and target <engine-luatest-deps>
# as a side effect.
_add_test_suite_target(${TEST_SUITE_NAME}
  LABELS "${TEST_SUITE_NAME};regression;"
  DEPENDS gh_6986 gh_9965
)

list(APPEND TEST_ENV
  "LUA_PATH=${LUATEST_LUA_PATH}"
  "LUA_CPATH=${LUA_CPATH}"
  "BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}"
  "VARDIR=${VARDIR}"
  "${TARANTOOL_ASAN_OPTIONS}"
)

file(GLOB tests ${CMAKE_CURRENT_SOURCE_DIR} *_test.lua)
foreach(test_path ${tests})
  get_filename_component(test_name ${test_path} NAME)
  # FIXME: By default, GLOB lists directories.
  # Directories are omitted in the result if LIST_DIRECTORIES
  # is set to false. New in version CMake 3.3.
  if(${test_name} STREQUAL ${TEST_SUITE_NAME})
    continue()
  endif()
  add_luatest_test(${TEST_SUITE_NAME} ${test_name} "${TEST_ENV}" FALSE)
endforeach()
