set(BENCHMARK_VERSION v1.9.0)
set(BENCHMARK_HASH 21a2604efeded8b4cbabc72f3e1c7a2a)
set(BENCHMARK_INSTALL_DIR ${BUNDLED_LIBS_INSTALL_DIR}/benchmark-prefix)
set(BENCHMARK_INCLUDE_DIR ${BENCHMARK_INSTALL_DIR}/src/include)

set(BENCHMARK_LIB ${BENCHMARK_INSTALL_DIR}/build/src/libbenchmark.a)
set(BENCHMARK_LIB_MAIN ${BENCHMARK_INSTALL_DIR}/build/src/libbenchmark_main.a)

list(APPEND BENCHMARK_CMAKE_FLAGS
     "-DBENCHMARK_ENABLE_TESTING=OFF"
     "-DBENCHMARK_ENABLE_LTO=OFF"
     "-DBENCHMARK_USE_LIBCXX=OFF"
     "-DBENCHMARK_ENABLE_GTEST_TESTS=OFF"
     "-DBENCHMARK_ENABLE_LIBPFM=OFF"
     # By default, this library is built in Debug. Propagate the
     # non-Debug build for benchmarks (benchmarks aren't built in
     # the Debug mode).
     "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
)

include(ExternalProject)
ExternalProject_Add(bundled-benchmark-project
    PREFIX ${BENCHMARK_INSTALL_DIR}
    SOURCE_DIR ${BENCHMARK_INSTALL_DIR}/src
    BINARY_DIR ${BENCHMARK_INSTALL_DIR}/build
    STAMP_DIR ${BENCHMARK_INSTALL_DIR}/stamp
    URL_MD5 ${BENCHMARK_HASH}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    URL https://github.com/google/benchmark/archive/refs/tags/${BENCHMARK_VERSION}.tar.gz
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -B <BINARY_DIR> -S <SOURCE_DIR>
            -G ${CMAKE_GENERATOR} ${BENCHMARK_CMAKE_FLAGS}
    BUILD_COMMAND cd <BINARY_DIR> && ${CMAKE_MAKE_PROGRAM} -j
    INSTALL_COMMAND ""
    CMAKE_GENERATOR ${CMAKE_GENERATOR}
    BUILD_BYPRODUCTS ${BENCHMARK_LIB} ${BENCHMARK_LIB_MAIN}
)

add_library(bundled-benchmark STATIC IMPORTED GLOBAL)
set_target_properties(bundled-benchmark PROPERTIES IMPORTED_LOCATION
  ${BENCHMARK_LIB})
add_dependencies(bundled-benchmark bundled-benchmark-project)

add_library(bundled-benchmark-main STATIC IMPORTED GLOBAL)
set_target_properties(bundled-benchmark-main PROPERTIES IMPORTED_LOCATION
  ${BENCHMARK_LIB_MAIN})
add_dependencies(bundled-benchmark-main bundled-benchmark-project)

add_custom_target(bundled-libbenchmark
    DEPENDS bundled-benchmark bundled-benchmark-main)
set(BENCHMARK_LIBRARIES bundled-benchmark-main bundled-benchmark)
set(BENCHMARK_INCLUDE_DIRS ${BENCHMARK_INCLUDE_DIR})
set(benchmark_FOUND TRUE)
# Propagate to the parent scope for small perf tests.
set(BENCHMARK_LIBRARIES ${BENCHMARK_LIBRARIES} PARENT_SCOPE)
set(benchmark_FOUND ${benchmark_FOUND} PARENT_SCOPE)
set(BENCHMARK_INCLUDE_DIRS ${BENCHMARK_INCLUDE_DIRS} PARENT_SCOPE)

unset(BENCHMARK_VERSION)
unset(BENCHMARK_HASH)
unset(BENCHMARK_INSTALL_DIR)
unset(BENCHMARK_INCLUDE_DIR)
unset(BENCHMARK_CMAKE_FLAGS)

message(STATUS "Using bundled Google Benchmark")
