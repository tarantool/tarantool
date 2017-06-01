# - Find the readline include files and libraries
# - Include finding of termcap or curses
#
# READLINE_FOUND
# READLINE_INCLUDE_DIR
# READLINE_LIBRARIES
#

find_package(Curses)
if(NOT CURSES_FOUND)
    find_package(Termcap)
endif()

if (DEFINED READLINE_ROOT)
  set(_FIND_OPTS NO_CMAKE NO_CMAKE_SYSTEM_PATH)
  find_library(READLINE_LIBRARY
    NAMES readline
    HINTS ${READLINE_ROOT}/lib
    ${_FIND_OPTS})
  find_path(READLINE_INCLUDE_DIR
    NAMES readline/readline.h
    HINTS ${READLINE_ROOT}/include ${_FIND_OPTS})
else()
  find_library(READLINE_LIBRARY NAMES readline)
  find_path(READLINE_INCLUDE_DIR readline/readline.h)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Readline
    REQUIRED_VARS READLINE_INCLUDE_DIR READLINE_LIBRARY)
set(READLINE_INCLUDE_DIRS ${READLINE_INCLUDE_DIR})
set(READLINE_LIBRARIES ${READLINE_LIBRARY})

if(READLINE_FOUND)
  if(EXISTS ${READLINE_INCLUDE_DIR}/readline/rlconf.h)
      check_library_exists(${READLINE_LIBRARY} rl_catch_sigwinch ""
          HAVE_GNU_READLINE)
      if(HAVE_GNU_READLINE)
          find_package_message(GNU_READLINE "Detected GNU Readline"
              "${HAVE_GNU_READLINE}")
      endif()
  endif()
  if(CURSES_FOUND)
    set(READLINE_LIBRARIES ${READLINE_LIBRARIES} ${CURSES_LIBRARIES})
    set(READLINE_INCLUDE_DIRS ${READLINE_INCLUDE_DIRS} ${CURSES_INCLUDE_DIRS})
  elseif(TERMCAP_FOUND)
    set(READLINE_LIBRARIES ${READLINE_LIBRARIES} ${TERMCAP_LIBRARIES})
    set(READLINE_INCLUDE_DIRS ${READLINE_INCLUDE_DIRS} ${TERMCAP_INCLUDE_DIRS})
  endif()
endif(READLINE_FOUND)

mark_as_advanced(READLINE_INCLUDE_DIRS READLINE_LIBRARIES)
