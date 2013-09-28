#
# A macro to build the bundled liblua-cjson
macro(libcjson_build)
    set(cjson_src ${PROJECT_SOURCE_DIR}/third_party/lua-cjson/lua_cjson.c 
                  ${PROJECT_SOURCE_DIR}/third_party/lua-cjson/strbuf.c 
                  ${PROJECT_SOURCE_DIR}/third_party/lua-cjson/fpconv.c)

    add_library(cjson STATIC ${cjson_src})

    if (ENABLE_DTRACE AND NOT TARGET_OS_DARWIN)
        set(cjson_obj_dir ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/cjson.dir/third_party/lua-cjson)
        set(dtrace_obj ${DTRACE_O_DIR}/cjson_dtrace.o)
        set(cjson_objs
            ${cjson_obj_dir}/fpconv.c.o
            ${cjson_obj_dir}/lua_cjson.c.o
            ${cjson_obj_dir}/strbuf.c.o
        )
        add_custom_command(TARGET cjson
            PRE_LINK
            COMMAND cp ${cjson_obj_dir}/lua_cjson.c.o ${DTRACE_O_DIR}/
            COMMAND ${DTRACE} -G -s ${DTRACE_D_FILE} -o ${dtrace_obj} ${cjson_objs}
        )
        set(DTRACE_OBJS ${DTRACE_OBJS} ${DTRACE_O_DIR}/lua_cjson.c.o)

        foreach(tmp_o in ${dtrace_obj} ${cjson_objs})
            set_source_files_properties(${tmp_o}
                PROPERTIES
                EXTERNAL_OBJECT true
                GENERATED true
            )
        endforeach(tmp_o)

        add_library(cjson_dtrace STATIC ${cjson_objs} ${dtrace_obj})
        set_target_properties(cjson_dtrace PROPERTIES LINKER_LANGUAGE C)

	set(LIBCJSON_LIBRARIES cjson_dtrace)

        unset(cjson_obj_dir)
        unset(cjson_objs)
        unset(dtrace_obj)
    else()
        set(LIBCJSON_LIBRARIES cjson)
    endif()

    set(LIBCJSON_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party/lua-cjson)

    message(STATUS "Use bundled Lua-CJSON library: ${LIBCJSON_LIBRARIES}")

    unset(lua_cjson_src)
endmacro(libcjson_build)

