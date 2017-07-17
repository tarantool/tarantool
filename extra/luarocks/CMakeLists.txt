configure_file(site_config.lua.cmake site_config.lua @ONLY)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/luarocks/src/luarocks
    DESTINATION ${MODULE_LUADIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/site_config.lua
    DESTINATION ${MODULE_LUADIR}/luarocks/
    PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_READ
    GROUP_READ WORLD_READ)
