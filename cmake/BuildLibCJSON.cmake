#
# A macro to build the bundled liblua-cjson
macro(libcjson_build)
    set(cjson_src ${PROJECT_SOURCE_DIR}/third_party/lua-cjson/lua_cjson.c 
                  ${PROJECT_SOURCE_DIR}/third_party/lua-cjson/strbuf.c 
                  ${PROJECT_SOURCE_DIR}/third_party/lua-cjson/fpconv.c)

    if (CC_HAS_WNO_UNDEFINED_INLINE)
        # inline function 'fpconv_init' is not defined [-Wundefined-inline]
        set_source_files_properties(${cjson_src} PROPERTIES
            COMPILE_FLAGS "-Wno-undefined-inline")
    endif()
    add_library(cjson STATIC ${cjson_src})

    set(LIBCJSON_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party/lua-cjson)
    set(LIBCJSON_LIBRARIES cjson)

    message(STATUS "Use bundled Lua-CJSON library: ${LIBCJSON_LIBRARIES}")

    unset(lua_cjson_src)
endmacro(libcjson_build)

