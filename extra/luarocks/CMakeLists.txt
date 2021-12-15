if(BUILD_STATIC)
    set(LUAROCKS_CONFIG hardcoded_static.lua.cmake)
else()
    set(LUAROCKS_CONFIG hardcoded.lua.cmake)
endif()

configure_file(${LUAROCKS_CONFIG} hardcoded.lua @ONLY)

if(NOT EMBED_LUAROCKS)
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/luarocks/src/luarocks
        DESTINATION ${MODULE_LUADIR})
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/hardcoded.lua
        DESTINATION ${MODULE_LUADIR}/luarocks/core
        PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_READ
        GROUP_READ WORLD_READ)
endif()
