set(api_headers
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
    ${CMAKE_SOURCE_DIR}/src/say.h
    ${CMAKE_SOURCE_DIR}/src/coeio.h
    ${CMAKE_SOURCE_DIR}/src/lua/utils.h
    ${CMAKE_SOURCE_DIR}/src/box/func.h)
rebuild_module_api(${api_headers})
