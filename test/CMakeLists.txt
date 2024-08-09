enable_tnt_compile_flags()

set(CTEST_OUTPUT_ON_FAILURE TRUE)
set(CTEST_PARALLEL_LEVEL 4)

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

set(UNIT_TEST_TARGETS "")

add_subdirectory(app)
add_subdirectory(app-tap)
add_subdirectory(app-luatest/lib)
add_subdirectory(box)
add_subdirectory(box-tap)
add_subdirectory(box-luatest)
add_subdirectory(engine-luatest)
add_subdirectory(sql-tap)
add_subdirectory(sql-luatest)
if(ENABLE_FUZZER)
    add_subdirectory(fuzz)
endif()
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
# TO BE REMOVED
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/small
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_CURRENT_BINARY_DIR}/../src/lib/small/test/
        ${CMAKE_CURRENT_BINARY_DIR}/small
        COMMENT Create the symlink for libsmall test binaries)

# TO BE REMOVED
add_custom_target(symlink_libsmall_test_binaries ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small)

add_custom_target(build-unit-tests
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small
            ${UNIT_TEST_TARGETS}
    COMMENT "Building unit tests"
)

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
    DEPENDS test
            test-unit
            test-func
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

message(STATUS "Add integration tests")
file(GLOB_RECURSE tests ${CMAKE_CURRENT_SOURCE_DIR} *.test.lua *.test.py)
foreach(path ${tests})
  get_filename_component(test_name ${path} NAME)
  get_filename_component(basedir ${path} DIRECTORY)
  get_filename_component(suite ${basedir} NAME)
  add_test(NAME "test/${suite}/${test_name}"
           COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py
                   --builddir=${PROJECT_BINARY_DIR}
                   --force
                   ${path}
  )
  set_property(TEST "test/${suite}/${test_name}" PROPERTY LABELS integration)
endforeach()
