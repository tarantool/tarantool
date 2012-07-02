
if (NOT HAVE_MEMMEM)
    set (misc_opt_sources ${misc_opt_sources} memmem.c)
endif()

if (NOT HAVE_MEMRCHR)
    set (misc_opt_sources ${misc_opt_sources} memrchr.c)
endif()

add_library (misc STATIC crc32.c proctitle.c qsort_arg.c ${misc_opt_sources})
if (NOT TARGET_OS_DEBIAN_FREEBSD) 
    if (TARGET_OS_FREEBSD)
      set_source_files_properties(proctitle.c PROPERTIES
           COMPILE_FLAGS "-DHAVE_SETPROCTITLE")
    endif()
endif()

add_subdirectory(coro)
add_subdirectory(gopt)

macro (luajit_build)
    set (luajit_buildoptions BUILDMODE=static)
    set (luajit_copt ${CCOPT})
    if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        set (luajit_buildoptions ${luajit_buildoptions} CCDEBUG=-ggdb)
        set (luajit_copt ${luajit_copt} -O1)
        set (luajit_buildoptions ${luajit_buildoptions} XCFLAGS='-DLUA_USE_APICHECK -DLUA_USE_ASSERT')
    else ()
        set (luajit_copt ${luajit_copt} -O2)
    endif()
    set (luajit_copt ${luajit_copt} -I${PROJECT_SOURCE_DIR}/libobjc)
    set (luajit_cc ${CMAKE_C_COMPILER})
    if (NOT luajit_cc)
        set (luajit_cc cc)
        message (STATUS "LuaJIT will compile with default C compiler (cc)")
    endif()
    set (luajit_buildoptions ${luajit_buildoptions} CC="${luajit_cc}" TARGET_CC="${luajit_cc}" CCOPT="${luajit_copt}")
    set (luajit_buildoptions ${luajit_buildoptions} Q='')
    if (${PROJECT_BINARY_DIR} STREQUAL ${PROJECT_SOURCE_DIR})
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/luajit/src/libluajit.a
            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/luajit
            COMMAND $(MAKE) clean
            COMMAND $(MAKE) -C src -t buildvm_x86.h buildvm_arm.h
                            buildvm_x64.h buildvm_x64win.h buildvm_ppc.h
                            buildvm_ppcspe.h
            COMMAND $(MAKE) -C src ${luajit_buildoptions}
            DEPENDS ${CMAKE_SOURCE_DIR}/CMakeCache.txt
        )
    else()
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/luajit
            COMMAND mkdir ${PROJECT_BINARY_DIR}/third_party/luajit
        )
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/luajit/src/libluajit.a
            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/luajit
            COMMAND cp -r ${PROJECT_SOURCE_DIR}/third_party/luajit/* .
            COMMAND $(MAKE) clean
            COMMAND $(MAKE) -C src -t buildvm_x86.h buildvm_arm.h
                            buildvm_x64.h buildvm_x64win.h buildvm_ppc.h
                            buildvm_ppcspe.h
            COMMAND $(MAKE) -C src ${luajit_buildoptions}
            DEPENDS ${PROJECT_BINARY_DIR}/CMakeCache.txt ${PROJECT_BINARY_DIR}/third_party/luajit
        )
    endif()
    add_custom_target(libluajit
        DEPENDS ${PROJECT_BINARY_DIR}/third_party/luajit/src/libluajit.a
    )
    unset (luajit_buildoptions)
endmacro()

macro(libobjc_build)
    if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        set (extra_cflags "-g -O0 -fno-inline")
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
    add_custom_target(libobjc
        DEPENDS ${PROJECT_BINARY_DIR}/third_party/libobjc/libobjc.a
    )
    unset (extra_cflags)
    unset (extra_ldlags)
endmacro()

#
# building shipped luajit only if there is no
# usable system one (see cmake/luajit.cmake) or by demand.
#
if (ENABLE_BUNDLED_LUAJIT)
    luajit_build()
endif()
libobjc_build()
