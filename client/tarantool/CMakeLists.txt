
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})
include(FindReadline)

if (NOT READLINE_FOUND)
    message(FATAL_ERROR "readline library not found.")
endif()

set(cli "tarantool")
set(cli_sources main.c opt.c query.c print.c buf.c cli.c pager.c store.c)
set(cli_libs gopt ${READLINE_LIBRARIES} tb)

include_directories(${READLINE_INCLUDE_DIR})

add_definitions("-DTB_LOCAL=${PROJECT_SOURCE_DIR}/connector/c/tb/lib")

list(APPEND cli_sources ${CMAKE_SOURCE_DIR}/src/errcode.c)

add_executable(${cli} ${cli_sources})
set_source_files_compile_flags("TARANTOOL" ${cli_sources})
target_link_libraries (${cli} ${cli_libs})

install(TARGETS ${cli} DESTINATION bin)

