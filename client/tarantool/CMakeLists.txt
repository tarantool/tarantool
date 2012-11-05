project(tnt_cli)

include(./FindReadline.cmake)

if (NOT READLINE_FOUND)
    message(FATAL_ERROR "readline library not found.")
endif()

set (cli "tarantool")
set (cli_sources tc.c tc_opt.c tc_admin.c tc_query.c tc_print.c tc_cli.c tc_store.c)
set (cli_libs tntrpl tntnet tntsql tnt gopt ${cli_deps})

include_directories(${READLINE_INCLUDE_DIR})
add_executable(${cli} ${cli_sources} ${CMAKE_SOURCE_DIR}/src/errcode.c)
set_target_properties(${cli} PROPERTIES COMPILE_FLAGS "${core_cflags}")
target_link_libraries (${cli} ${READLINE_LIBRARIES})

install (TARGETS ${cli} DESTINATION bin)
