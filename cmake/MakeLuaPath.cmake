# tarantool_make_lua_path provides a convenient way to define
# LUA_PATH and LUA_CPATH variables.
#
# Example usage:
#
#   tarantool_make_lua_path(LUA_PATH
#     PATHS
#       ./?.lua
#       ${CMAKE_BINARY_DIR}/?.lua
#       ${CMAKE_CURRENT_SOURCE_DIR}/?.lua
#   )
#
# This will give you the string:
#    "./?.lua;${CMAKE_BINARY_DIR}/?.lua;${CMAKE_CURRENT_SOURCE_DIR}/?.lua;;"

function(tarantool_make_lua_path path)
  set(prefix ARG)
  set(noValues)
  set(singleValues)
  set(multiValues PATHS)

  # FIXME: if we update to CMake >= 3.5, can remove this line.
  include(CMakeParseArguments)
  cmake_parse_arguments(${prefix}
                        "${noValues}"
                        "${singleValues}"
                        "${multiValues}"
                        ${ARGN})

  set(_MAKE_LUA_PATH_RESULT "")

  foreach(inc ${ARG_PATHS})
    # XXX: If one joins two strings with the semicolon, the value
    # automatically becomes a list. I found a single working
    # solution to make result variable be a string via "escaping"
    # the semicolon right in string interpolation.
    set(_MAKE_LUA_PATH_RESULT "${_MAKE_LUA_PATH_RESULT}${inc}\;")
  endforeach()

  if("${_MAKE_LUA_PATH_RESULT}" STREQUAL "")
    message(FATAL_ERROR "No paths are given to <tarantool_make_lua_path> helper.")
  endif()

  # XXX: This is the sentinel semicolon having special meaning
  # for LUA_PATH and LUA_CPATH variables. For more info, see the
  # link below:
  # https://www.lua.org/manual/5.1/manual.html#pdf-LUA_PATH
  set(${path} "${_MAKE_LUA_PATH_RESULT}\;" PARENT_SCOPE)
  # XXX: Unset the internal variable to not spoil CMake cache.
  # Study the case in CheckIPOSupported.cmake, that affected this
  # module: https://gitlab.kitware.com/cmake/cmake/-/commit/4b82977
  unset(_MAKE_LUA_PATH_RESULT)
endfunction()
