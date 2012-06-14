
# function to build tarantool clients that use connector/c
# 
function(tarantool_client client_name)
	set (client_sources ${ARGV})
	list(REMOVE_ITEM client_sources ${client_name})
	add_executable(${client_name} ${client_sources}
		       ${CMAKE_SOURCE_DIR}/src/errcode.c)
	set (client_libs tntrpl tntnet tntsql tnt)
	set_target_properties(${client_name} PROPERTIES COMPILE_FLAGS "${core_cflags}")
	target_link_libraries (${client_name} ${client_libs})
endfunction()

foreach (client ${TARANTOOL_CLIENTS})
	add_subdirectory(${client})
endforeach()
