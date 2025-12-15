set(C_TEST_SUFFIX .c_test)

# Build libtest.

set(TEST_LIB_NAME "test")
add_library(libtest STATIC EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/test.c)
target_include_directories(libtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(libtest PROPERTIES
  COMPILE_FLAGS "-Wall -Wextra"
  OUTPUT_NAME "${TEST_LIB_NAME}"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

# TARGET_C_FLAGS is required here to be sure that headers like
# lj_arch.h in compiled test are consistent with the LuaJIT library
# to link.
AppendFlags(TESTS_C_FLAGS ${TARGET_C_FLAGS})
# Use directory for absolute path to the Lua script helper. So,
# test binary can be run from any directory.
AppendFlags(TESTS_C_FLAGS "-D__LJ_TEST_DIR__='\"${CMAKE_CURRENT_SOURCE_DIR}\"'")

set(TEST_SUITE_NAME "tarantool-c-tests")

# The proxy CMake target with all targets that build C tests.
# This is needed because targets for each C test are generated
# at the same time as CMake tests, and all prerequisites must
# already exist at this moment.
add_custom_target(tarantool-c-tests-build)

# XXX: The call produces both test and target
# <tarantool-c-tests-deps> as a side effect.
add_test_suite_target(tarantool-c-tests
  LABELS ${TEST_SUITE_NAME}
  # XXX: LUAJIT_TEST_DEPS is not needed, we know dependencies
  # in advance.
  DEPENDS libluajit libtest tarantool-c-tests-build
)

set(CTEST_SRC_SUFFIX ".test.c")
file(GLOB tests "${CMAKE_CURRENT_SOURCE_DIR}/*${CTEST_SRC_SUFFIX}")
foreach(test_source ${tests})
  # Get test name without suffix. Needed to set OUTPUT_NAME.
  get_filename_component(exe ${test_source} NAME_WE)

  # Test requires static build, since it inspects internal
  # symbols.
  if(exe STREQUAL "gh-8594-sysprof-ffunc-crash" AND
     BUILDMODE STREQUAL "dynamic")
    continue()
  endif()

  add_executable(${exe} EXCLUDE_FROM_ALL ${test_source})
  target_include_directories(${exe} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LUAJIT_SOURCE_DIR}
  )
  set_target_properties(${exe} PROPERTIES
    COMPILE_FLAGS "${TESTS_C_FLAGS}"
    OUTPUT_NAME "${exe}${C_TEST_SUFFIX}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
  target_link_libraries(${exe} libtest ${LUAJIT_LIBRARY})
  add_dependencies(tarantool-c-tests-build ${exe})

  # Generate CMake tests.
  set(test_title "test/${TEST_SUITE_NAME}/${exe}${C_TEST_SUFFIX}")
  set(test_command ${CMAKE_CURRENT_BINARY_DIR}/${exe}${C_TEST_SUFFIX})

  if(LUAJIT_USE_VALGRIND)
    set(test_command ${LUAJIT_TEST_VALGRIND_COMMAND} ${test_command})
  endif()

  add_test(NAME ${test_title}
    COMMAND ${test_command}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )

  set_tests_properties(${test_title} PROPERTIES
    LABELS ${TEST_SUITE_NAME}
    DEPENDS tarantool-c-tests-deps
  )
endforeach()
