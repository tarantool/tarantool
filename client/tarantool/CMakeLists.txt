
project(tnt_cli)

# checking for readline library
#
find_library(WITH_READLINE_LIB NAMES "readline")
find_library(WITH_READLINE_HISTORY_LIB NAMES "history")

if (NOT WITH_READLINE_LIB OR NOT WITH_READLINE_HISTORY_LIB)
    message(FATAL_ERROR "readline library not found.")
endif()

set (cli "tarantool")
set (cli_sources tnt.c tnt_admin.c)
set (cli_libs tntsql tnt gopt readline history)

add_executable(${cli} ${cli_sources} ${CMAKE_SOURCE_DIR}/core/errcode.c)
set_target_properties(${cli} PROPERTIES COMPILE_FLAGS "${core_cflags}")
target_link_libraries (${cli} ${cli_libs})

install (PROGRAMS ${cli} DESTINATION bin)
