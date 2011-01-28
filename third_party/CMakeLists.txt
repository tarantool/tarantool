add_library (misc STATIC crc32.c daemon.c proctitle.c qsort_arg.c)

if (TARGET_OS_FREEBSD)
  set_source_files_properties(proctitle.c PROPERTIES
       COMPILE_FLAGS "-DHAVE_SETPROCTITLE")
endif()

add_subdirectory(coro)
add_subdirectory(gopt)
