# The function creates a command that generates a file with the Itanium ABI
# mangled names of the global operator new/delete replacements defined by
# mimalloc's `mimalloc-new-delete.h', by compiling a stand-in source with
# the same signatures and dumping its symbols with `nm'.
#
# Needed on macOS: ld64's -exported_symbols_list is a strict allow-list, unlike
# GNU ld's --dynamic-list, which auto-exports symbols that shadow ones from
# linked shared libraries. Without an explicit entry, the overrides become
# private extern and no longer interpose into libc++.1.dylib's own weak new/
# delete definitions, so memory allocated by mimalloc can end up freed by the
# system allocator, and vice versa.
function(BuildMimallocNewDeleteSymbols MimallocExports)
    set(symbols_source
        ${PROJECT_SOURCE_DIR}/extra/mimalloc_new_delete_symbols.cc)
    set(symbols_object
        ${PROJECT_BINARY_DIR}/extra/mimalloc_new_delete_symbols.o)
    set(symbols_compile_flags -std=c++17)
    if(TARGET_OS_DARWIN AND NOT "${CMAKE_OSX_SYSROOT}" STREQUAL "")
        # Bypasses CMake's own per-target compile rules, which would normally
        # add this automatically; without it, the standard C++ headers aren't
        # found at all on macOS (no /usr/include there, only inside the SDK).
        list(APPEND symbols_compile_flags ${CMAKE_C_SYSROOT_FLAG}
            ${CMAKE_OSX_SYSROOT})
    endif()
    list(APPEND NM_OPTIONS
        --format=just-symbols
        --quiet
        --defined-only
        --extern-only
    )
    if("${CMAKE_NM}" MATCHES "llvm-nm")
        list(APPEND NM_OPTIONS --export-symbols)
    endif()
    add_custom_command(
        OUTPUT ${MimallocExports}
        COMMAND ${CMAKE_CXX_COMPILER} ${symbols_compile_flags}
            -c ${symbols_source} -o ${symbols_object}
        # `nm' on Darwin reports symbols as they sit in the object file, which
        # already carries the platform's own leading underscore in addition to
        # the Itanium "_Z" mangling prefix (e.g. "__Znwm"). `extra/mkexports'
        # unconditionally prepends one more underscore for Darwin exports, so
        # strip the one nm just reported to end up with a single one again.
        COMMAND ${CMAKE_NM} ${NM_OPTIONS} ${symbols_object} | sed -e "s/^_//"
            > ${MimallocExports}
        DEPENDS ${symbols_source}
        COMMENT "Generate a file with exported mimalloc new/delete symbols"
    )
endfunction()
