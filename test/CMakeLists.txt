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

if(POLICY CMP0037)
    if(CMAKE_VERSION VERSION_LESS 3.11)
        # cmake below 3.11 reserves name test. Use old policy.
        # https://cmake.org/cmake/help/v3.11/release/3.11.html#other-changes
        cmake_policy(SET CMP0037 OLD)
    else()
        # Starting from cmake 3.11 name test reserved in special
        # cases and can be used as target name.
        cmake_policy(SET CMP0037 NEW)
    endif()
endif(POLICY CMP0037)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/small
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${PROJECT_SOURCE_DIR}/src/lib/small/test/
        ${CMAKE_CURRENT_BINARY_DIR}/small
        COMMENT Create a symlink for libsmall to fix out-of-source tests)
add_custom_target(symlink_small_tests ALL
   DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small)

add_custom_target(test
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small
            LuaJIT-test
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py
        --builddir=${PROJECT_BINARY_DIR}
        --vardir=${PROJECT_BINARY_DIR}/test/var)

add_custom_target(test-force
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small
            LuaJIT-test
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py
        --builddir=${PROJECT_BINARY_DIR}
        --vardir=${PROJECT_BINARY_DIR}/test/var
        --force)

add_subdirectory(app)
add_subdirectory(app-tap)
add_subdirectory(box)
add_subdirectory(box-tap)
add_subdirectory(sql-tap)
if(ENABLE_FUZZER)
    add_subdirectory(fuzz)
endif()
add_subdirectory(unit)
