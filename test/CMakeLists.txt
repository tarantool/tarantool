enable_tnt_compile_flags()

include_directories(${LUAJIT_INCLUDE_DIRS})
include_directories(${MSGPUCK_INCLUDE_DIRS})

function(build_module module files)
    add_library(${module} SHARED ${files})
    target_link_libraries(${module} ${MSGPUCK_LIBRARIES})
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
    cmake_policy(SET CMP0037 OLD)
endif(POLICY CMP0037)

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
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py
        --builddir=${PROJECT_BINARY_DIR}
        small/
        unit/)

add_custom_target(test-unit-force
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small
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

add_custom_target(test
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small
            LuaJIT-test
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py
        --builddir=${PROJECT_BINARY_DIR})

add_custom_target(test-force
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/small
            LuaJIT-test
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py
        --builddir=${PROJECT_BINARY_DIR}
        --force)

add_subdirectory(app)
add_subdirectory(app-tap)
add_subdirectory(box)
add_subdirectory(unit)
