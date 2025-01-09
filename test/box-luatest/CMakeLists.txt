include_directories(${MSGPUCK_INCLUDE_DIRS})
build_module(gh_4799_lib gh_4799_fix_c_stored_functions_call.c)
target_link_libraries(gh_4799_lib msgpuck)
build_module(gh_6506_lib gh_6506_wakeup_writing_to_wal_fiber.c)
build_module(gh_9131_lib gh_9131_net_box_self_call_stored_func.c)
target_link_libraries(gh_9131_lib msgpuck)
build_module(libcallnum libcallnum.c)

tarantool_make_lua_path(LUA_CPATH
  PATHS
  ${CMAKE_CURRENT_BINARY_DIR}/?${CMAKE_SHARED_LIBRARY_SUFFIX}
)

set(TEST_SUITE_NAME "box-luatest")

message(STATUS "Add test suite ${TEST_SUITE_NAME}")

# XXX: The call produces both test and target <box-tap-deps>
# as a side effect.
_add_test_suite_target(${TEST_SUITE_NAME}
  LABELS "${TEST_SUITE_NAME};regression;"
  DEPENDS gh_4799_lib gh_6506_lib gh_9131_lib libcallnum
)

list(APPEND TEST_ENV
  "LUA_PATH=${LUATEST_LUA_PATH}"
  "SOURCEDIR=${PROJECT_SOURCE_DIR}"
  "BUILDDIR=${PROJECT_BINARY_DIR}"
  "VARDIR=${VARDIR}"
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
  set(test_title "test/${TEST_SUITE_NAME}/${test_name}")
  add_test(NAME ${test_title}
           COMMAND ${TARANTOOL_BIN}
                   # Disable stdout buffering,
                   # see https://github.com/tarantool/test-run/issues/119.
                   -e "io.stdout:setvbuf('no')"
                   ${LUATEST_COMMAND_WITH_FLAGS}
                   ${test_title}
           WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
  set_tests_properties(${test_title} PROPERTIES
    ENVIRONMENT "${TEST_ENV}"
    LABELS "${TEST_SUITE_NAME};regression;"
    DEPENDS ${TEST_SUITE_NAME}-deps
    FAIL_REGULAR_EXPRESSION "${CTEST_FAIL_REGEXP}"
  )
endforeach()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  set_tests_properties(
    # See test/box-luatest/suite.ini.
    test/box-luatest/gh_6819_iproto_watch_not_implemented_test.lua
    PROPERTIES DISABLED TRUE
  )
endif()
