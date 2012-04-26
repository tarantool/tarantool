
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
    if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
        set (luajit_buildoptions ${luajit_buildoptions} CCOPT=-O0)
        set (luajit_buildoptions ${luajit_buildoptions} CCDEBUG=-ggdb)
        set (luajit_buildoptions ${luajit_buildoptions} XCFLAGS='-DLUA_USE_APICHECK -DLUA_USE_ASSERT')
    endif()
    set (luajit_buildoptions ${luajit_buildoptions} Q='')
    if (${PROJECT_BINARY_DIR} STREQUAL ${PROJECT_SOURCE_DIR})
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/third_party/luajit/src/libluajit.a
            WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/third_party/luajit
            COMMAND $(MAKE) clean
            COMMAND $(MAKE) -C src -t buildvm_x86.h buildvm_arm.h
                            buildvm_x64.h buildvm_x64win.h
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
                            buildvm_x64.h buildvm_x64win.h
            COMMAND $(MAKE) -C src ${luajit_buildoptions}
            DEPENDS ${PROJECT_BINARY_DIR}/CMakeCache.txt ${PROJECT_BINARY_DIR}/third_party/luajit
        )
    endif()
    add_custom_target(libluajit
        DEPENDS ${PROJECT_BINARY_DIR}/third_party/luajit/src/libluajit.a
    )
    unset (luajit_buildoptions)
endmacro()

#
# building shipped luajit only if there is no
# usable system one (see cmake/luajit.cmake) or by demand.
#
if (ENABLE_BUNDLED_LUAJIT)
    luajit_build()
endif()
