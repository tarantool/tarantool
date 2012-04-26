
project(tnt_cli)

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
find_library(NCURSES_LIB NAMES "ncurses")
if (NCURSES_LIB)
    set(cli_deps ${cli_deps} ${NCURSES_LIB})
else()
    find_library(TERMCAP_LIB NAMES "termcap")
    if (TERMCAP_LIB)
        set(cli_deps ${cli_deps} ${TERMCAP_LIB})
    endif()
endif()

set (cli "tarantool")
set (cli_sources tnt.c tnt_admin.c)
set (cli_libs tntnet tntsql tnt gopt ${cli_deps})

add_executable(${cli} ${cli_sources} ${CMAKE_SOURCE_DIR}/src/errcode.c)
set_target_properties(${cli} PROPERTIES COMPILE_FLAGS "${core_cflags}")
target_link_libraries (${cli} ${cli_libs})

install (TARGETS ${cli} DESTINATION bin)
