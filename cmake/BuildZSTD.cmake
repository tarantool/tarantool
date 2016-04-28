macro(zstd_build)
    add_library(zstd STATIC
        third_party/zstd/lib/zstd_compress.c
        third_party/zstd/lib/zstd_decompress.c
        third_party/zstd/lib/zdict.c
        third_party/zstd/lib/zbuff.c
        third_party/zstd/lib/fse.c
        third_party/zstd/lib/huff0.c
        third_party/zstd/lib/divsufsort.c
    )
    set(ZSTD_LIBRARIES zstd)
    set(ZSTD_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/lib)
    find_package_message(ZSTD "Using bundled ZSTD"
        "${ZSTD_LIBRARIES}:${ZSTD_INCLUDE_DIRS}")
    add_dependencies(build_bundled_libs zstd)
endmacro(zstd_build)
