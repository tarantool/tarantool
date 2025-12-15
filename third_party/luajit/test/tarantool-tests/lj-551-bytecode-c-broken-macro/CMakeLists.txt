enable_language(CXX)

set(LUA_FILE ${CMAKE_CURRENT_SOURCE_DIR}/script.lua)

make_lua_path(LUA_PATH
  PATHS
    ${LUAJIT_SOURCE_DIR}/?.lua
    ${LUAJIT_SOURCE_DIR}/jit/?.lua
)

macro(BuildTestBCLib file_ext)
  set(LIB_NAME "script_${file_ext}")
  set(LUA_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}.lua)
  set(EXT_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/${LIB_NAME}.${file_ext})

  # XXX: Module has to be copied, since bytecode dumper uses its
  # name within the exported symbol.
  add_custom_target(export_bc_${file_ext}
    COMMAND ${CMAKE_COMMAND} -E copy ${LUA_FILE} ${LUA_SOURCE}
    COMMAND ${CMAKE_COMMAND} -E env
      LUA_PATH=${LUA_PATH} ${LUAJIT_TEST_BINARY} -b ${LUA_SOURCE} ${EXT_SOURCE}
    COMMAND ${CMAKE_COMMAND} -E remove ${LUA_SOURCE}
    DEPENDS ${LUAJIT_TEST_BINARY} ${LUA_FILE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    BYPRODUCTS ${EXT_SOURCE}
    COMMENT "Exporting bytecode to a ${file_ext} file"
    VERBATIM
  )

  get_filename_component(test_name ${CMAKE_CURRENT_SOURCE_DIR} NAME)
  BuildTestCLib(${LIB_NAME} ${EXT_SOURCE} ${test_name}.test.lua)
  add_dependencies(${LIB_NAME} export_bc_${file_ext})
endmacro()

BuildTestBCLib("c")
BuildTestBCLib("cc")
