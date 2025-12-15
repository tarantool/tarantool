# Get the libc version installed in the system.
macro(GetLibCVersion output)
  find_program(ECHO echo)
  if(NOT ECHO)
    message(FATAL_ERROR "`echo' is not found")
  endif()
  # Try to directly parse the version.
  execute_process(
    COMMAND ${ECHO} "#include <gnu/libc-version.h>"
    COMMAND ${CMAKE_C_COMPILER} -E -dM -
    OUTPUT_VARIABLE LIB_C_INFO
    ERROR_VARIABLE ERROR_MSG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE RES
  )

  if(NOT RES EQUAL 0)
    message(FATAL_ERROR
            "gnu/libc-version preprocessing has failed: '${ERROR_MSG}'")
  endif()

  string(REGEX MATCH "__GLIBC__ ([0-9]+)" MATCH ${LIB_C_INFO})
  if(MATCH)
    set(GLIBC_MAJOR ${CMAKE_MATCH_1})
  else()
    message(FATAL_ERROR "Can't determine GLIBC_MAJOR version.")
  endif()

  string(REGEX MATCH "__GLIBC_MINOR__ ([0-9]+)" MATCH ${LIB_C_INFO})
  if(MATCH)
    set(GLIBC_MINOR ${CMAKE_MATCH_1})
  else()
    message(FATAL_ERROR "Can't determine GLIBC_MINOR version.")
  endif()

  set(${output} "${GLIBC_MAJOR}.${GLIBC_MINOR}")

  unset(ECHO)
  unset(CMAKE_MATCH_1)
  unset(GLIBC_MAJOR)
  unset(GLIBC_MINOR)
  unset(MATCH)
  unset(RES)
  unset(ERROR_MSG)
  unset(LIB_C_INFO)
endmacro()
