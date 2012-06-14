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

# trying to resolve readline dependencies
#
if (CURSES_FOUND)
    set(cli_deps ${cli_deps} ${CURSES_LIBRARIES})
else()
    # readline could be compiled with termcap
    find_library(TERMCAP_LIB NAMES "termcap")
	if (TERMCAP_LIB)
	set(cli_deps ${cli_deps} ${TERMCAP_LIB})
    endif()
endif()

set (cli "tarantool")
set (cli_sources tc.c tc_opt.c tc_admin.c tc_query.c tc_print.c tc_cli.c tc_wal.c)
set (cli_libs tntrpl tntnet tntsql tnt gopt ${cli_deps})

add_executable(${cli} ${cli_sources} ${CMAKE_SOURCE_DIR}/src/errcode.c)
set_target_properties(${cli} PROPERTIES COMPILE_FLAGS "${core_cflags}")
target_link_libraries (${cli} ${cli_libs})

install (TARGETS ${cli} DESTINATION bin)
