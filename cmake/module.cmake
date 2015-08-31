# A helper function to extract public API
function(rebuild_module_api)
    set (dstfile "${CMAKE_CURRENT_BINARY_DIR}/tarantool.h")
    set (tmpfile "${dstfile}.new")
    set (headers)
    # Get absolute path for header files (required of out-of-source build)
    foreach (header ${ARGN})
        if (IS_ABSOLUTE ${header})
            list(APPEND headers ${header})
        else()
            list(APPEND headers ${CMAKE_CURRENT_SOURCE_DIR}/${header})
        endif()
    endforeach()

    add_custom_command(OUTPUT ${dstfile}
        COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/tarantool_header.h > ${tmpfile}
        COMMAND cat ${headers} | ${CMAKE_SOURCE_DIR}/extra/apigen >> ${tmpfile}
        COMMAND ${CMAKE_C_COMPILER}
            -I ${CMAKE_SOURCE_DIR}/src -I ${CMAKE_BINARY_DIR}/src
            -E ${CMAKE_SOURCE_DIR}/src/box/errcode.h |
            grep "enum box_error_code" >> ${tmpfile}
        COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/tarantool_footer.h >> ${tmpfile}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${tmpfile} ${dstfile}
        COMMAND ${CMAKE_COMMAND} -E remove ${tmpfile}
        DEPENDS ${srcfiles} ${CMAKE_SOURCE_DIR}/src/box/errcode.h
                ${CMAKE_CURRENT_SOURCE_DIR}/tarantool_header.h
                ${CMAKE_CURRENT_SOURCE_DIR}/tarantool_footer.h
        )

    add_custom_target(api ALL DEPENDS ${srcfiles} ${dstfile})
    install(FILES ${dstfile} DESTINATION ${MODULE_INCLUDEDIR})
endfunction()
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/tarantool.h" PROPERTIES GENERATED HEADER_FILE_ONLY)
