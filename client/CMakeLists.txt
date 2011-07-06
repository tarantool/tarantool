
# function to build tarantool clients that use connector/c
# without install
function(tarantool_client client_name)
	set (client_sources ${ARGV})
	list(REMOVE_ITEM client_sources ${client_name})
	add_executable(${client_name} ${client_sources}
		${CMAKE_SOURCE_DIR}/core/errcode.c)
	set (client_libs client)
	set_target_properties(${client_name} PROPERTIES COMPILE_FLAGS
	"${core_cflags}")
	target_link_libraries (${client_name} ${client_libs})
endfunction()

# function to build tarantool clients with installation
#
function(tarantool_client_install client_name)
	set (client_sources ${ARGV})
	list(REMOVE_ITEM client ${client_name})
	add_executable(${client_name} ${client_sources}
		${CMAKE_SOURCE_DIR}/core/errcode.c)
	set (client_libs client)
	set_target_properties(${client_name} PROPERTIES COMPILE_FLAGS
	"${core_cflags}")
	target_link_libraries (${client_name} ${client_libs})
	install (TARGETS ${client_name} DESTINATION bin)
endfunction()

foreach (client ${TARANTOOL_CLIENTS})
	add_subdirectory(${client})
endforeach()
