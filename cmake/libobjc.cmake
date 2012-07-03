#
# A macro to build the bundled libobjc
#
macro(libobjc_build)
    if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        set (extra_cflags "${CC_DEBUG_OPT} -O0 -fno-inline")
        set (extra_ldflags "")
    else ()
        set (extra_cflags "-O3 -Wno-unused-result")
        set (extra_ldflags "-s")
    endif()
    if (${TARGET_OS_LINUX})
        set (extra_cflags "${extra_cflags} -D_GNU_SOURCE")
    endif()
    if (CMAKE_COMPILER_IS_CLANG)
        set (extra_cflags "${extra_cflags} -Wno-deprecated-objc-isa-usage")
    endif()
    if (NOT (${PROJECT_BINARY_DIR} STREQUAL ${PROJECT_SOURCE_DIR}))
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/libobjc
            COMMAND mkdir ${PROJECT_BINARY_DIR}/third_party/libobjc
            COMMAND cp -r ${PROJECT_SOURCE_DIR}/third_party/libobjc/*
                ${PROJECT_BINARY_DIR}/third_party/libobjc
        )
    endif()
    add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/libobjc/libobjc.a
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/libobjc
        COMMAND $(MAKE) clean
        COMMAND $(MAKE) EXTRA_CFLAGS=""${extra_cflags}"" EXTRA_LDFLAGS=""${extra_ldflags}""
        DEPENDS ${PROJECT_BINARY_DIR}/CMakeCache.txt
    )
    add_custom_target(libobjc ALL
        DEPENDS ${PROJECT_BINARY_DIR}/third_party/libobjc/libobjc.a
    )
    add_dependencies(build_bundled_libs libobjc)
    unset (extra_cflags)
    unset (extra_ldlags)
endmacro()

if (TARGET_OS_DARWIN)
    set (LIBOBJC_LIB objc)
else()
    set (LIBOBJC_LIB "${PROJECT_BINARY_DIR}/third_party/libobjc/libobjc.a")
    include_directories("${PROJECT_SOURCE_DIR}/third_party/libobjc")
    if (CMAKE_C_COMPILER_IS_CLANG)
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-nonfragile-abi")
        SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-objc-legacy-dispatch")
    endif()
    libobjc_build()
endif()
