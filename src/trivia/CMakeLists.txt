set(api_headers
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
    ../say.h
    ../coeio.h
    ../lua/utils.h)
apigen(${api_headers})
