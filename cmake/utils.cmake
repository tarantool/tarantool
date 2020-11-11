macro(add_compile_flags langs)
    foreach(_lang ${langs})
        string (REPLACE ";" " " _flags "${ARGN}")
        set ("CMAKE_${_lang}_FLAGS" "${CMAKE_${_lang}_FLAGS} ${_flags}")
        unset (${_lang})
        unset (${_flags})
    endforeach()
endmacro(add_compile_flags)

macro(set_source_files_compile_flags)
    foreach(file ${ARGN})
        get_filename_component(_file_ext ${file} EXT)
        set(_lang "")
        if ("${_file_ext}" STREQUAL ".m")
            set(_lang OBJC)
            # CMake believes that Objective C is a flavor of C++, not C,
            # and uses g++ compiler for .m files.
            # LANGUAGE property forces CMake to use CC for ${file}
            set_source_files_properties(${file} PROPERTIES LANGUAGE C)
        elseif("${_file_ext}" STREQUAL ".mm")
            set(_lang OBJCXX)
        endif()

        if (_lang)
            get_source_file_property(_flags ${file} COMPILE_FLAGS)
            if ("${_flags}" STREQUAL "NOTFOUND")
                set(_flags "${CMAKE_${_lang}_FLAGS}")
            else()
                set(_flags "${_flags} ${CMAKE_${_lang}_FLAGS}")
            endif()
            # message(STATUS "Set (${file} ${_flags}")
            set_source_files_properties(${file} PROPERTIES COMPILE_FLAGS
                "${_flags}")
        endif()
    endforeach()
    unset(_file_ext)
    unset(_lang)
endmacro(set_source_files_compile_flags)

# A helper function to compile *.lua source into *.lua.c sources
function(lua_source varname filename)
    if (IS_ABSOLUTE "${filename}")
        string (REPLACE "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}"
            genname "${filename}")
        set (srcfile "${filename}")
        set (tmpfile "${genname}.new.c")
        set (dstfile "${genname}.c")
    else(IS_ABSOLUTE "${filename}")
        set (srcfile "${CMAKE_CURRENT_SOURCE_DIR}/${filename}")
        set (tmpfile "${CMAKE_CURRENT_BINARY_DIR}/${filename}.new.c")
        set (dstfile "${CMAKE_CURRENT_BINARY_DIR}/${filename}.c")
    endif(IS_ABSOLUTE "${filename}")
    get_filename_component(module ${filename} NAME_WE)
    get_filename_component(_name ${dstfile} NAME)
    string(REGEX REPLACE "${_name}$" "" dstdir ${dstfile})
    if (IS_DIRECTORY ${dstdir})
    else()
        file(MAKE_DIRECTORY ${dstdir})
    endif()

    ADD_CUSTOM_COMMAND(OUTPUT ${dstfile}
        COMMAND ${ECHO} 'const char ${module}_lua[] =' > ${tmpfile}
        COMMAND ${CMAKE_BINARY_DIR}/extra/txt2c ${srcfile} >> ${tmpfile}
        COMMAND ${ECHO} '\;' >> ${tmpfile}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${tmpfile} ${dstfile}
        COMMAND ${CMAKE_COMMAND} -E remove ${tmpfile}
        DEPENDS ${srcfile} txt2c libluajit)

    set(var ${${varname}})
    set(${varname} ${var} ${dstfile} PARENT_SCOPE)
endfunction()

function(bin_source varname srcfile dstfile)
    set(var ${${varname}})
    set (srcfile "${CMAKE_CURRENT_SOURCE_DIR}/${srcfile}")
    set (dstfile "${CMAKE_CURRENT_BINARY_DIR}/${dstfile}")
    set(${varname} ${var} "${dstfile}" PARENT_SCOPE)
    set (tmpfile "${dstfile}.tmp")
    get_filename_component(module ${dstfile} NAME_WE)

    ADD_CUSTOM_COMMAND(OUTPUT ${dstfile}
        COMMAND ${ECHO} 'const unsigned char ${module}_bin[] = {' > ${tmpfile}
        COMMAND ${CMAKE_BINARY_DIR}/extra/bin2c "${srcfile}" >> ${tmpfile}
        COMMAND ${ECHO} '}\;' >> ${tmpfile}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${tmpfile} ${dstfile}
        COMMAND ${CMAKE_COMMAND} -E remove ${tmpfile}
        DEPENDS ${srcfile} bin2c)

endfunction()

#
# Whether a file is descendant to a directory.
#
# If the file is the directory itself, the answer is FALSE.
#
function(file_is_in_directory varname file dir)
    file(RELATIVE_PATH file_relative "${dir}" "${file}")

    # Tricky point: one may find <STREQUAL ".."> and
    # <MATCHES "^\\.\\./"> if-branches quite similar and coalesce
    # them as <MATCHES "^\\.\\.">. However it'll match paths like
    # "..." or "..foo/bar", whose are definitely descendant to
    # the directory.
    if (file_relative STREQUAL "")
        # <file> and <dir> is the same directory.
        set(${varname} FALSE PARENT_SCOPE)
    elseif (file_relative STREQUAL "..")
        # <dir> inside a <file> (so it is a directory too), not
        # vice versa.
        set(${varname} FALSE PARENT_SCOPE)
    elseif (file_relative MATCHES "^\\.\\./")
        # <file> somewhere outside of the <dir>.
        set(${varname} FALSE PARENT_SCOPE)
    else()
        # <file> is descendant to <dir>.
        set(${varname} TRUE PARENT_SCOPE)
    endif()
endfunction()
