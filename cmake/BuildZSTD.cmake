macro(zstd_build)
    add_library(zstd STATIC
        third_party/zstd/lib/compress/zstd_compress.c
        third_party/zstd/lib/decompress/zstd_decompress.c
    )
    set(ZSTD_LIBRARIES zstd)
    set(ZSTD_INCLUDE_DIRS
	    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/lib
	    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zstd/lib/common)
    target_include_directories(zstd PUBLIC ${ZSTD_INCLUDE_DIRS})
    find_package_message(ZSTD "Using bundled ZSTD"
        "${ZSTD_LIBRARIES}:${ZSTD_INCLUDE_DIRS}")
    add_dependencies(build_bundled_libs zstd)
endmacro(zstd_build)
