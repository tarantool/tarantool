macro(libxxhash_build)
    set(xxhash_src third_party/xxHash/xxhash.c)

    if (CC_HAS_WNO_IMPLICIT_FALLTHROUGH)
        set_source_files_properties(${xxhash_src}
                PROPERTIES COMPILE_FLAGS
                -Wno-implicit-fallthrough)
    endif()

    # Specify "XXH_NAMESPACE" to prevent symbol clash with zstd
    # that also uses xxhash.
    # Remaining properties are the same as for zstd
    # (see cmake/BuildZSTD.cmake).
    set_source_files_properties(${xxhash_src}
            PROPERTIES COMPILE_FLAGS "-Ofast -DXXH_NAMESPACE=tnt_")

    add_library(xxhash STATIC ${xxhash_src})
    set(XXHASH_LIBRARIES xxhash)
    set(XXHASH_INCLUDE_DIRS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xxHash)
    include_directories(${XXHASH_INCLUDE_DIRS})
endmacro(libxxhash_build)
