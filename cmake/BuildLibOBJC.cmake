#
# A macro to build the bundled libobjc
#
macro(libobjc_build)
    if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        set (extra_cflags "${CC_DEBUG_OPT} -O0")
        if (CMAKE_COMPILER_IS_GNUCC)
            set (extra_cflags "${extra_cflags} -fno-inline")
        endif()
        set (extra_ldflags "")
    else()
        set (extra_cflags "-O3")
        if (CC_HAS_WNO_UNUSED_RESULT)
            set (extra_cflags "${extra_cflags} -Wno-unused-result")
        endif()
        set (extra_ldflags "-s")
    endif()
    if (TARGET_OS_LINUX)
        set (extra_cflags "${extra_cflags} -D_GNU_SOURCE")
       if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "686")
            set (extra_cflags "${extra_cflags} -march=i586")
       endif()
    endif()
    if (CMAKE_COMPILER_IS_CLANG)
        set (extra_cflags "${extra_cflags} -Wno-deprecated-objc-isa-usage")
        set (extra_cflags "${extra_cflags} -Wno-objc-root-class")
    endif()
    set (extra_cflags "${extra_cflags} -Wno-attributes")
    set (extra_cflags "${CMAKE_C_FLAGS} ${extra_cflags}")
    if (HAVE_NON_C99_PTHREAD_H)
        set (extra_cflags "${extra_cflags} -fgnu89-inline")
    endif()
    separate_arguments(extra_cflags)
    separate_arguments(extra_ldflags)
    if (NOT (${PROJECT_BINARY_DIR} STREQUAL ${PROJECT_SOURCE_DIR}))
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/libobjc
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/third_party/libobjc"
            COMMAND cp -r ${PROJECT_SOURCE_DIR}/third_party/libobjc/*
                ${PROJECT_BINARY_DIR}/third_party/libobjc
        )
    endif()
    add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/libobjc/libobjc.a
        WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/libobjc
        COMMAND $(MAKE) clean
        COMMAND $(MAKE) CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER} EXTRA_CFLAGS="${extra_cflags}" EXTRA_LDFLAGS="${extra_ldflags}"
        DEPENDS ${PROJECT_BINARY_DIR}/third_party/libobjc
                ${PROJECT_BINARY_DIR}/CMakeCache.txt
    )
    add_custom_target(objc ALL
        DEPENDS ${PROJECT_BINARY_DIR}/third_party/libobjc/libobjc.a
    )

    set(LIBOBJC_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party/libobjc)
    set(LIBOBJC_LIBRARIES ${PROJECT_BINARY_DIR}/third_party/libobjc/libobjc.a)

    message(STATUS "Use bundled libobjc includes: ${LIBOBJC_INCLUDE_DIR}")
    message(STATUS "Use bundled libobjc library: ${LIBOBJC_LIBRARIES}")

    unset (extra_cflags)
    unset (extra_ldlags)
endmacro()
