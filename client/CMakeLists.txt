
enable_tnt_compile_flags()

# function to build tarantool clients that use connector/c
# 
function(tarantool_client client_name)
	set (client_sources ${ARGV})
	list(REMOVE_ITEM client_sources ${client_name})
	add_executable(${client_name} ${client_sources}
		       ${CMAKE_SOURCE_DIR}/src/errcode.c)
	set (client_libs tntrpl tntnet tntsql tnt)
	target_link_libraries (${client_name} ${client_libs})
endfunction()

include_directories("${PROJECT_SOURCE_DIR}/connector/c/include")

foreach (client ${TARANTOOL_CLIENTS})
	add_subdirectory(${client})
endforeach()
