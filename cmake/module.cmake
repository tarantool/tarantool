# A helper function to extract public API
function(rebuild_module_api)
    set (dstfile "${CMAKE_CURRENT_BINARY_DIR}/module.h")
    set (tmpfile "${dstfile}.new")
    set (errcodefile "${CMAKE_CURRENT_BINARY_DIR}/errcode.i")
    set (headers)
    # Get absolute path for header files (required of out-of-source build)
    foreach (header ${ARGN})
        if (IS_ABSOLUTE ${header})
            list(APPEND headers ${header})
        else()
            list(APPEND headers ${CMAKE_CURRENT_SOURCE_DIR}/${header})
        endif()
    endforeach()

    set (cflags ${CMAKE_C_FLAGS})
    separate_arguments(cflags)
    # Pass sysroot settings on OSX
    if (NOT "${CMAKE_OSX_SYSROOT}" STREQUAL "")
        set (cflags ${cflags} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT})
    endif()
    add_custom_command(OUTPUT ${dstfile}
        COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/module_header.h > ${tmpfile}
        COMMAND cat ${headers} | ${CMAKE_SOURCE_DIR}/extra/apigen >> ${tmpfile}
        COMMAND ${CMAKE_C_COMPILER}
            ${cflags}
            -I ${CMAKE_SOURCE_DIR}/src -I ${CMAKE_BINARY_DIR}/src
            -E ${CMAKE_SOURCE_DIR}/src/box/errcode.h > ${errcodefile}
        COMMAND
            grep "enum box_error_code" ${errcodefile} >> ${tmpfile}
        COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/module_footer.h >> ${tmpfile}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${tmpfile} ${dstfile}
        COMMAND ${CMAKE_COMMAND} -E remove ${errcodefile} ${tmpfile}
        DEPENDS ${srcfiles} ${CMAKE_SOURCE_DIR}/src/box/errcode.h
                ${CMAKE_CURRENT_SOURCE_DIR}/module_header.h
                ${CMAKE_CURRENT_SOURCE_DIR}/module_footer.h
        )

    add_custom_target(api ALL DEPENDS ${srcfiles} ${dstfile})
    install(FILES ${dstfile} DESTINATION ${MODULE_INCLUDEDIR})
endfunction()
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/module.h" PROPERTIES GENERATED HEADER_FILE_ONLY)
