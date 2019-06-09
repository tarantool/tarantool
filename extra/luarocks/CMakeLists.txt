configure_file(hardcoded.lua.cmake hardcoded.lua @ONLY)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/third_party/luarocks/src/luarocks
    DESTINATION ${MODULE_LUADIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/hardcoded.lua
    DESTINATION ${MODULE_LUADIR}/luarocks/core
    PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_READ
    GROUP_READ WORLD_READ)
