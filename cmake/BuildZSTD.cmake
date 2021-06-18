macro(zstd_build)
    set(zstd_src
        third_party/zstd/lib/common/zstd_common.c
        third_party/zstd/lib/common/entropy_common.c
        third_party/zstd/lib/common/error_private.c
        third_party/zstd/lib/common/pool.c
        third_party/zstd/lib/common/xxhash.c
        third_party/zstd/lib/common/fse_decompress.c
        third_party/zstd/lib/common/debug.c
        third_party/zstd/lib/decompress/zstd_decompress.c
        third_party/zstd/lib/decompress/huf_decompress.c
        third_party/zstd/lib/decompress/zstd_ddict.c
        third_party/zstd/lib/decompress/zstd_decompress_block.c
        third_party/zstd/lib/compress/zstd_double_fast.c
        third_party/zstd/lib/compress/zstd_fast.c
        third_party/zstd/lib/compress/zstd_lazy.c
        third_party/zstd/lib/compress/zstd_opt.c
        third_party/zstd/lib/compress/zstd_compress.c
        third_party/zstd/lib/compress/zstd_ldm.c
        third_party/zstd/lib/compress/zstdmt_compress.c
        third_party/zstd/lib/compress/huf_compress.c
        third_party/zstd/lib/compress/fse_compress.c
        third_party/zstd/lib/compress/hist.c
        third_party/zstd/lib/compress/zstd_compress_superblock.c
        third_party/zstd/lib/compress/zstd_compress_sequences.c
        third_party/zstd/lib/compress/zstd_compress_literals.c
    )

    if (CC_HAS_WNO_IMPLICIT_FALLTHROUGH)
        set_source_files_properties(${zstd_src}
            PROPERTIES COMPILE_FLAGS -Wno-implicit-fallthrough)
    endif()
    set_source_files_properties(${zstd_src}
        PROPERTIES COMPILE_FLAGS -Ofast)

    add_library(zstd STATIC ${zstd_src})
    set(ZSTD_LIBRARIES zstd)
    set(ZSTD_INCLUDE_DIRS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/lib
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/lib/common)
    include_directories(${ZSTD_INCLUDE_DIRS})
    find_package_message(ZSTD "Using bundled ZSTD"
        "${ZSTD_LIBRARIES}:${ZSTD_INCLUDE_DIRS}")
endmacro(zstd_build)
