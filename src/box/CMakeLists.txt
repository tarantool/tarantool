if (TARGET_OS_DARWIN)
    set(module_link_flags "-pagezero_size 10000 -image_base 100000000")
endif()



aux_source_directory(${CMAKE_SOURCE_DIR}/src/box/lua lua_sources)


set(lua_sources
	${lua_sources}
	${CMAKE_SOURCE_DIR}/src/box/lua/box.net.lua
	${CMAKE_SOURCE_DIR}/src/box/lua/box.lua
	${CMAKE_SOURCE_DIR}/src/box/lua/box.autoincrement.lua
	${CMAKE_SOURCE_DIR}/src/box/lua/box.bless_space.lua
	${CMAKE_SOURCE_DIR}/src/box/lua/box.counter.lua
	${CMAKE_SOURCE_DIR}/src/box/lua/box.fiber.lua

)

add_custom_command(
	OUTPUT ${CMAKE_BINARY_DIR}/src/box/box.lua.c
	DEPENDS ${lua_sources} ${CMAKE_SOURCE_DIR}/extra/pack_lua
	WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/src/box
	COMMAND ${CMAKE_SOURCE_DIR}/extra/pack_lua -o ${CMAKE_BINARY_DIR}/src/box/box.lua.c ${lua_sources}
)


set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${lua_sources})

tarantool_module("box" tuple.m index.m hash_index.m tree_index.m space.m
    port.m request.m txn.m box.m box_lua.m box_lua_space.m
    bitset_index.m
    ${CMAKE_BINARY_DIR}/src/box/box.lua.c
)

target_link_libraries(tarantool_box bitset)
