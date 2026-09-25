macro(add_compile_flags langs)
    foreach(_lang ${langs})
        string (REPLACE ";" " " _flags "${ARGN}")
        set ("CMAKE_${_lang}_FLAGS" "${CMAKE_${_lang}_FLAGS} ${_flags}")
        unset (_lang)
        unset (_flags)
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
function(lua_source varname filename symbolname)
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
    get_filename_component(_name ${dstfile} NAME)
    string(REGEX REPLACE "${_name}$" "" dstdir ${dstfile})
    if (IS_DIRECTORY ${dstdir})
    else()
        file(MAKE_DIRECTORY ${dstdir})
    endif()

    ADD_CUSTOM_COMMAND(OUTPUT ${dstfile}
        COMMAND ${ECHO} 'const char ${symbolname}[] =' > ${tmpfile}
        COMMAND ${PROJECT_BINARY_DIR}/extra/txt2c ${srcfile} >> ${tmpfile}
        COMMAND ${ECHO} '\;' >> ${tmpfile}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${tmpfile} ${dstfile}
        COMMAND ${CMAKE_COMMAND} -E remove ${tmpfile}
        DEPENDS ${srcfile} txt2c libluajit)

    set(var ${${varname}})
    set(${varname} ${var} ${dstfile} PARENT_SCOPE)
endfunction()

# A helper function to unpack list with filenames and variable names
# and compile *.lua source into *.lua.c sources using lua_source() function.
# Function expects a list 'source_list' with pairs of paths to a Lua source file
# and a symbol names that should be used in C source code for that file.
function(lua_multi_source var_name source_list)
    set(source_list ${source_list} ${ARGN})
    list(LENGTH source_list len)
    math(EXPR len "${len} - 1")
    set(_sources)
    foreach(filename_idx RANGE 0 ${len} 2)
        list(GET source_list ${filename_idx} filename)
        math(EXPR symbolname_idx "${filename_idx} + 1")
        list(GET source_list ${symbolname_idx} symbolname)
        lua_source(_sources ${filename} ${symbolname})
    endforeach()
    set(${var_name} ${${var_name}};${_sources} PARENT_SCOPE)
endfunction()

function(bin_source varname srcfile dstfile symbolname)
    set(var ${${varname}})
    set (srcfile "${CMAKE_CURRENT_SOURCE_DIR}/${srcfile}")
    set (dstfile "${CMAKE_CURRENT_BINARY_DIR}/${dstfile}")
    set(${varname} ${var} "${dstfile}" PARENT_SCOPE)
    set (tmpfile "${dstfile}.tmp")

    ADD_CUSTOM_COMMAND(OUTPUT ${dstfile}
        COMMAND ${ECHO} 'const unsigned char ${symbolname}[] = {' > ${tmpfile}
        COMMAND ${PROJECT_BINARY_DIR}/extra/bin2c "${srcfile}" >> ${tmpfile}
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

# list() has TRANSFORM option but only since 3.12.
function(list_add_prefix
    list_in
    prefix
    list_out
)
    set(result "")
    foreach(i ${${list_in}})
        list(APPEND result "${prefix}${i}")
    endforeach()
    set(${list_out} ${result} PARENT_SCOPE)
endfunction()

# string() has JOIN option but only since 3.12.
function(string_join
    glue
    string_out
)
    set(result "")
    set(is_first_item TRUE)
    foreach(item IN LISTS ARGN)
        if(is_first_item)
            set(result "${item}")
            set(is_first_item FALSE)
        else()
            set(result "${result}${glue}${item}")
        endif()
    endforeach()
    set(${string_out} ${result} PARENT_SCOPE)
endfunction()

# ${EP_MAKE_COMMAND} is the make command for ExternalProject_Add() steps
# of a library that has its own makefiles. Such a library is configured
# by its own configure script, and its build and install steps are make
# runs.
#
# The value is `make`, except with the Makefile generators, where it is
# `$(MAKE)`. CMake writes that text into the generated rule as is, and
# make expands it at build time. GNU make also treats a rule mentioning
# $(MAKE) as a recursive call: the sub-make takes job slots from the job
# server of the main build instead of running its recipes one by one, and
# it obeys -n, -t and -q.
#
# The condition matches any generator whose name contains `Makefiles`:
# `Unix Makefiles`, `NMake Makefiles`, the MinGW and MSYS variants. That
# is deliberate: $(MAKE) belongs to the makefile language rather than to
# GNU make (POSIX defines MAKE, NMAKE has it for recursive calls), and
# ExternalProject uses the same test internally for its default steps.
# Non-Makefile generators produce no makefile to expand it; ninja
# refuses to load a build file that contains $(MAKE).
#
# ${CMAKE_MAKE_PROGRAM} looks like a fit, but it is the build tool of the
# main build rather than a make: with the Ninja generator it is ninja,
# which has nothing to do where the library's configure left makefiles.
if(CMAKE_GENERATOR MATCHES "Makefiles")
    set(EP_MAKE_COMMAND "$(MAKE)")
else()
    set(EP_MAKE_COMMAND "make")
endif()
