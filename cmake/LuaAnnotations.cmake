# lua_stub() exports EmmyLua annotations from Lua sources into a stub file.
#
# lua_stub(<module> FILES <file>...)
#
# Gathers EmmyLua annotation comments (---@...) from the given files
# together with bodyless declarations they annotate and writes the result
# as a single `---@meta` file named `<module>.lua` into the
# `${PROJECT_BINARY_DIR}/emmylua` directory.
#
# The file is generated at build time by the `emmylua-stubs` target. The
# extraction runs in a Lua helper (tools/gen-emmylua-stubs.lua) executed
# by the bundled LuaJIT.
function(lua_stub name)
    cmake_parse_arguments(ARG "" "" "FILES" ${ARGN})
    if(NOT ARG_FILES)
        message(FATAL_ERROR "lua_stub(${name}): FILES is not specified")
    endif()

    set(_out_file "${PROJECT_BINARY_DIR}/emmylua/${name}.lua")
    get_filename_component(_out_dir "${_out_file}" DIRECTORY)
    get_filename_component(_extractor
        "${PROJECT_SOURCE_DIR}/tools/gen-emmylua-stubs.lua"
        ABSOLUTE)

    set(_abs_files)
    foreach(_file IN LISTS ARG_FILES)
        get_filename_component(_abs "${_file}" ABSOLUTE)
        list(APPEND _abs_files "${_abs}")
    endforeach()

    add_custom_command(OUTPUT "${_out_file}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
        COMMAND ${LUAJIT_EXECUTABLE} "${_extractor}"
            --out "${_out_file}" ${_abs_files}
        DEPENDS ${_abs_files} "${_extractor}" luajit_static
        VERBATIM
        COMMENT "Generating EmmyLua stub ${name}")

    add_custom_target(${name}-stub DEPENDS "${_out_file}")
    if(NOT TARGET emmylua-stubs)
        add_custom_target(emmylua-stubs)
    endif()
    add_dependencies(emmylua-stubs ${name}-stub)
endfunction()
