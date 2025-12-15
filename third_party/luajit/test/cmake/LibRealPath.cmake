# Find a full path of a library used by a compiler and set it to
# the given variable.
macro(LibRealPath output lib)
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "LibRealPath macro is unsupported for OSX")
  endif()
  execute_process(
    # CMAKE_C_COMPILER is used because it has the same behaviour
    # as CMAKE_CXX_COMPILER, but CMAKE_CXX_COMPILER is not
    # required for building LuaJIT and can be missed in GH
    # Actions.
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=${lib}
    OUTPUT_VARIABLE LIB_LINK
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE RES
  )

  if(NOT RES EQUAL 0)
    message(FATAL_ERROR
      "Executing '${CMAKE_C_COMPILER} -print-file-name=${lib}' has failed"
    )
  endif()

  # GCC and Clang return a passed filename when a library is
  # not found.
  if(LIB_LINK STREQUAL ${lib})
    message(FATAL_ERROR "Library '${lib}' is not found")
  endif()

  # Fortunately, we are not interested in macOS here, so we can
  # use realpath. Beware, `realpath` always returns an exit code
  # equal to 0, so we cannot check if it fails.
  execute_process(
    COMMAND realpath ${LIB_LINK}
    OUTPUT_VARIABLE ${output}
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  unset(LIB_LINK)
  unset(RES)
endmacro()
