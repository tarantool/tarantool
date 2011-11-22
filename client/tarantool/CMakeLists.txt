
project(tnt_cli)

# checking for readline library
#
find_library(WITH_READLINE_LIB NAMES "readline")
find_library(WITH_READLINE_HISTORY_LIB NAMES "history")

if (NOT WITH_READLINE_LIB OR NOT WITH_READLINE_HISTORY_LIB)
    message(FATAL_ERROR "readline library not found.")
endif()

set (cli_deps readline history)

# trying to resolve readline dependencies
find_library(WITH_NCURSES_LIB NAMES "ncurses")
if (WITH_NCURSES_LIB)
	set(cli_deps ${cli_deps} ncurses)
endif()

find_library(WITH_TERMCAP_LIB NAMES "termcap")
if (WITH_TERMCAP_LIB)
	set(cli_deps ${cli_deps} termcap)
endif()

set (cli "tarantool")
set (cli_sources tnt.c tnt_admin.c)
set (cli_libs tntnet tntsql tnt gopt ${cli_deps})

add_executable(${cli} ${cli_sources} ${CMAKE_SOURCE_DIR}/core/errcode.c)
set_target_properties(${cli} PROPERTIES COMPILE_FLAGS "${core_cflags}")
target_link_libraries (${cli} ${cli_libs})

install (PROGRAMS ${cli} DESTINATION bin)
