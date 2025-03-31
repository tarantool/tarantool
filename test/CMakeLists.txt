enable_tnt_compile_flags()

include_directories(${LUAJIT_INCLUDE_DIRS})
include_directories(${MSGPUCK_INCLUDE_DIRS})

function(build_module module files)
    add_library(${module} SHARED ${files})
    set_target_properties(${module} PROPERTIES PREFIX "")
    add_dependencies(${module} api)
    if(TARGET_OS_DARWIN)
        set_target_properties(${module} PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
    endif(TARGET_OS_DARWIN)
endfunction()

add_compile_flags("C;CXX"
    "-Wno-unused-parameter")

# WARNING: This change affects current cmake variable scope and so
#          a user should care to don't use it in a top level scope.
# The dynamic libraries will be loaded from tarantool executable
# and will use symbols from it. So it is completely okay to have
# unresolved symbols at build time.
string(REPLACE "-Wl,--no-undefined" ""
    CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")

# luatest and test-run.py use so-called VARDIR, where Unix socket
# files for testing purposes are placed.
# By default, VARDIR in luatest and test-run.py set to "/tmp/t",
# however, we explicitly set it. It allows easily redefine it.
# Note, GHA workflows set VARDIR explicitly as well.
set(VARDIR $ENV{VARDIR})
if (NOT VARDIR)
  set(VARDIR "/tmp/t")
endif()

set(UNIT_TEST_TARGETS "")

# If the test output (stdout or stderr) matches these regular
# expressions the test will fail, regardless of the process exit
# code, see [1], [2], [3] and [4].
#
# 1. https://cmake.org/cmake/help/latest/prop_test/FAIL_REGULAR_EXPRESSION.html
# 2. https://testanything.org/tap-version-13-specification.html
# 3. https://github.com/tarantool/tarantool/blob/a4b4158f2196442308aba7df8f8e21b98b5319fc/src/lua/tap.lua
# 4. https://github.com/tarantool/luatest/blob/3a78617c2c4a611f6affc9bb5f2120d4e8ea563e/luatest/output/tap.lua
list(APPEND CTEST_FAIL_REGEXP
  "^not ok"
  "bad plan"
  "failed subtests"
)

string(RANDOM LENGTH 9 ALPHABET 0123456789 RANDOM_SEED)
message(STATUS "Random seed is ${RANDOM_SEED}")

list(APPEND LUATEST_COMMAND_WITH_FLAGS
    ${PROJECT_SOURCE_DIR}/test-run/lib/luatest/bin/luatest
    --no-clean
    --verbose
    --no-capture
    --output tap
)

tarantool_make_lua_path(LUATEST_LUA_PATH
  PATHS
    # Both paths below are used, some tests uses Lua modules from
    # another test suites.
    ${PROJECT_SOURCE_DIR}/?.lua
    ${PROJECT_SOURCE_DIR}/?/init.lua
    ${PROJECT_SOURCE_DIR}/test-run/lib/checks/?.lua
    ${PROJECT_SOURCE_DIR}/test-run/lib/luatest/?/init.lua
    ${PROJECT_SOURCE_DIR}/test-run/lib/luatest/?.lua
)

add_subdirectory(app)
add_subdirectory(app-tap)
add_subdirectory(app-luatest)
add_subdirectory(box)
add_subdirectory(box-tap)
add_subdirectory(box-luatest)
add_subdirectory(config-luatest)
add_subdirectory(engine-luatest)
add_subdirectory(metrics-luatest)
add_subdirectory(replication-luatest)
add_subdirectory(sql-tap)
add_subdirectory(sql-luatest)
add_subdirectory(tla)
add_subdirectory(vinyl-luatest)
if(ENABLE_FUZZER)
    add_subdirectory(fuzz)
endif()
add_subdirectory(fuzz/lua)
add_subdirectory(unit)
foreach(TEST_SUITE ${EXTRA_TEST_SUITES})
    add_subdirectory(
        "${EXTRA_TEST_SUITES_SOURCE_DIR}/${TEST_SUITE}"
        "${EXTRA_TEST_SUITES_BINARY_DIR}/${TEST_SUITE}")
endforeach()

# The symlink is needed for out-of-source builds. In the case of in-source
# builds ${CMAKE_CURRENT_BINARY_DIR} == ${PROJECT_SOURCE_DIR}/test and the
# symlink already exists. It creates the symlink if the destination doesn't
# exist.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/small
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_BINARY_DIR}/../src/lib/small/test/
        ${CMAKE_CURRENT_BINARY_DIR}/small
        COMMENT Create the symlink for libsmall test binaries)

add_custom_target(symlink_libsmall_test_binaries ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small)

add_custom_target(test-unit
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small
            ${UNIT_TEST_TARGETS}
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py
        --builddir=${PROJECT_BINARY_DIR}
        small/
        unit/)

add_custom_target(test-unit-force
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small
            ${UNIT_TEST_TARGETS}
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py
        --builddir=${PROJECT_BINARY_DIR}
        --force
        small/
        unit/)

add_custom_target(test-func
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py
        --builddir=${PROJECT_BINARY_DIR}
        --exclude small/
        --exclude unit/)

add_custom_target(test-func-force
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py
        --builddir=${PROJECT_BINARY_DIR}
        --exclude small/
        --exclude unit/
        --force)

add_custom_target(test-all
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small
            LuaJIT-test
            ${UNIT_TEST_TARGETS}
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py
        --builddir=${PROJECT_BINARY_DIR})

add_custom_target(test-force
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small
            LuaJIT-test
            ${UNIT_TEST_TARGETS}
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py
        --builddir=${PROJECT_BINARY_DIR}
        --force)

message(STATUS "Add regression tests")
add_custom_target(test-force-ctest
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small
            ${UNIT_TEST_TARGETS}
    COMMAND ${CMAKE_CTEST_COMMAND} ${TARANTOOL_CTEST_FLAGS}
        # Set the default test timeout in seconds.
        --timeout 600
        --label-exclude "\"performance|fuzzing\""
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    COMMENT "Running regression tests using CTest")

list(APPEND EXECUTED_WITHOUT_TEST_RUN
    app-luatest
    box-luatest
    config-luatest
    engine-luatest
    metrics-luatest
    replication-luatest
    sql-luatest
    vinyl-luatest
)

file(GLOB_RECURSE tests
  ${PROJECT_SOURCE_DIR}/test/*.test.lua
  ${PROJECT_SOURCE_DIR}/test/*.test.py
  ${PROJECT_SOURCE_DIR}/test/*_test.lua
  ${PROJECT_SOURCE_DIR}/test/*.test.sql
)
foreach(test_path ${tests})
  get_filename_component(test_name ${test_path} NAME)
  get_filename_component(basedir ${test_path} DIRECTORY)
  get_filename_component(suite ${basedir} NAME)
  # With CMake 3.3 or later, the `if` command supports
  # an IN_LIST operator.
  list (FIND EXECUTED_WITHOUT_TEST_RUN ${suite} _index)
  if(${_index} GREATER -1)
    continue()
  endif()
  set(test_title "test/${suite}/${test_name}")
  # Salt is needed to avoid intersection in vardir paths for
  # different tests. Salt length equal to 1 is enough and remember
  # that vardir is used for paths to Unix domain sockets, whose
  # paths cannot be longer 105 symbols.
  string(RANDOM LENGTH 1 ALPHABET 0123456789 SALT)
  add_test(NAME ${test_title}
           COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py
                   --builddir=${PROJECT_BINARY_DIR}
                   # Print TAP13 test output.
                   --verbose
                   # Print test-run logs to the terminal.
                   --debug
                   --vardir ${VARDIR}/${SALT}-${test_name}
                   --retries 0
                   ${suite}/${test_name}
  )
  set_tests_properties(${test_title} PROPERTIES
    LABELS "${suite};regression;"
    DEPENDS tarantool
    FAIL_REGULAR_EXPRESSION "${CTEST_FAIL_REGEXP}"
  )
endforeach()
