
project(tnt_cli)
include(FindCurses)

# checking for readline library
#
find_library(READLINE_LIB NAMES "readline")
find_library(READLINE_HISTORY_LIB NAMES "history")

if (NOT READLINE_LIB OR NOT READLINE_HISTORY_LIB)
    message(FATAL_ERROR "readline library not found.")
endif()

set (cli_deps ${READLINE_LIB} ${READLINE_HISTORY_LIB})

if (CURSES_FOUND)
    set(cli_deps ${cli_deps} ${CURSES_LIBRARIES})
else()
    message(FATAL_ERROR "curses library not found.")
endif()
find_library(TERMCAP_LIBRARY NAMES "termcap")
if (TERMCAP_LIBRARY)
    set(cli_deps ${cli_deps} ${TERMCAP_LIBRARY})
else()
    message(FATAL_ERROR "termcap library not found.")
endif()
# trying to resolve readline dependencies
#

set (cli "tarantool")
set (cli_sources tnt.c tnt_admin.c)
set (cli_libs tntnet tntsql tnt gopt ${cli_deps})

add_executable(${cli} ${cli_sources} ${CMAKE_SOURCE_DIR}/src/errcode.c)
set_target_properties(${cli} PROPERTIES COMPILE_FLAGS "${core_cflags}")
target_link_libraries (${cli} ${cli_libs})

install (TARGETS ${cli} DESTINATION bin)
