set(LIB_NAME "script")
set(LUA_FILE ${CMAKE_CURRENT_SOURCE_DIR}/${LIB_NAME}.lua)
set(C_FILE ${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}.c)

make_lua_path(LUA_PATH
  PATHS
    ${LUAJIT_SOURCE_DIR}/?.lua
    ${LUAJIT_SOURCE_DIR}/jit/?.lua
)

add_custom_target(export_bc
  COMMAND ${CMAKE_COMMAND} -E env
    LUA_PATH=${LUA_PATH} ${LUAJIT_TEST_BINARY} -b ${LUA_FILE} ${C_FILE}
  DEPENDS ${LUAJIT_TEST_BINARY} ${LUA_FILE}
  BYPRODUCTS ${C_FILE}
  COMMENT "Exporting bytecode to a C file"
  VERBATIM
)

get_filename_component(test_name ${CMAKE_CURRENT_SOURCE_DIR} NAME)
BuildTestCLib(${LIB_NAME} ${C_FILE} ${test_name}.test.lua)
add_dependencies(${LIB_NAME} export_bc)
