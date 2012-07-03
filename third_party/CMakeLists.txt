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
