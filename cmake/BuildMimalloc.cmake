#
# Build the bundled mimalloc allocator.
#
# mimalloc is built here as a static library without MI_OVERRIDE, so it does not
# intercept malloc/free, etc. It is used only to back a global C++ operator new
# (and operator delete) override, see `src/box/mimalloc_new_delete.cc'.
#
set(MIMALLOC_VERSION 3.5.3)
set(MIMALLOC_HASH 3b4a15153a59905995f7070296ed604bb5ccc00cabb8b93446931aff77224d47)
set(MIMALLOC_INSTALL_DIR ${BUNDLED_LIBS_INSTALL_DIR}/mimalloc-prefix)
set(MIMALLOC_INCLUDE_DIR ${MIMALLOC_INSTALL_DIR}/include)

# ASAN and valgrind need mimalloc to be built with the corresponding tracking
# support, otherwise they are blind to the memory it serves.
set(MIMALLOC_TRACK OFF)
set(MIMALLOC_CFLAGS ${DEPENDENCY_CFLAGS})
if(ENABLE_ASAN)
    set(MIMALLOC_TRACK ASAN)
elseif(ENABLE_VALGRIND)
    set(MIMALLOC_TRACK VALGRIND)
    # mimalloc's valgrind tracking needs the client request headers. Use the
    # bundled ones as not to depend on the system-wide headers.
    string(APPEND MIMALLOC_CFLAGS
        " -I${PROJECT_SOURCE_DIR}/src/lib/small/third_party")
endif()

# mimalloc appends the tracking mode and the build type to the library name
# unless the latter is a release-like one (mimalloc-debug, mimalloc-asan-debug,
# mimalloc-valgrind-debug, ...).
string(TOLOWER "${CMAKE_BUILD_TYPE}" MIMALLOC_BUILD_TYPE_LC)
set(MIMALLOC_LIBNAME mimalloc)
if(MIMALLOC_TRACK STREQUAL "VALGRIND")
    string(APPEND MIMALLOC_LIBNAME "-valgrind")
elseif(MIMALLOC_TRACK STREQUAL "ASAN")
    string(APPEND MIMALLOC_LIBNAME "-asan")
endif()
if(NOT MIMALLOC_BUILD_TYPE_LC MATCHES "^(release|relwithdebinfo|minsizerel|none)$")
    string(APPEND MIMALLOC_LIBNAME "-${MIMALLOC_BUILD_TYPE_LC}")
endif()

set(MIMALLOC_LIBRARY ${MIMALLOC_INSTALL_DIR}/lib/lib${MIMALLOC_LIBNAME}.a)

set(MIMALLOC_CMAKE_ARGS
    "-DCMAKE_INSTALL_PREFIX=${MIMALLOC_INSTALL_DIR}"
    "-DCMAKE_INSTALL_LIBDIR=lib"
    "-DCMAKE_INSTALL_INCLUDEDIR=include"
    "-DMI_INSTALL_TOPLEVEL=ON"
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
    "-DCMAKE_LINKER=${CMAKE_LINKER}"
    "-DCMAKE_AR=${CMAKE_AR}"
    "-DCMAKE_RANLIB=${CMAKE_RANLIB}"
    "-DCMAKE_NM=${CMAKE_NM}"
    "-DCMAKE_STRIP=${CMAKE_STRIP}"
    "-DCMAKE_C_FLAGS=${MIMALLOC_CFLAGS}"
    # In hardened mode, which enables -fPIE by default, the cmake checks don't
    # work without -fPIC.
    "-DCMAKE_REQUIRED_FLAGS=-fPIC"
    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    "-DMI_OVERRIDE=OFF"
    "-DMI_BUILD_SHARED=OFF"
    "-DMI_BUILD_STATIC=ON"
    "-DMI_BUILD_OBJECT=OFF"
    "-DMI_BUILD_TESTS=OFF"
    # mimalloc enables transparent huge pages by default on Linux. Disable it.
    "-DMI_ALLOW_THP=OFF"
    "-DMI_TRACK=${MIMALLOC_TRACK}")

ExternalProject_Add(bundled-mimalloc-project
    PREFIX ${MIMALLOC_INSTALL_DIR}
    SOURCE_DIR ${MIMALLOC_INSTALL_DIR}/src/mimalloc
    BINARY_DIR ${MIMALLOC_INSTALL_DIR}/src/mimalloc-build
    STAMP_DIR ${MIMALLOC_INSTALL_DIR}/src/mimalloc-stamp
    URL ${BACKUP_STORAGE}/mimalloc/mimalloc-v${MIMALLOC_VERSION}.tar.gz
    URL_HASH SHA256=${MIMALLOC_HASH}
    CMAKE_ARGS ${MIMALLOC_CMAKE_ARGS}
    BUILD_BYPRODUCTS ${MIMALLOC_LIBRARY}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

add_library(bundled-mimalloc STATIC IMPORTED GLOBAL)
set_target_properties(bundled-mimalloc PROPERTIES IMPORTED_LOCATION
    ${MIMALLOC_LIBRARY})
add_dependencies(bundled-mimalloc bundled-mimalloc-project)

message(STATUS "Using bundled mimalloc ${MIMALLOC_VERSION}")

unset(MIMALLOC_BUILD_TYPE_LC)
unset(MIMALLOC_LIBNAME)
unset(MIMALLOC_CMAKE_ARGS)
unset(MIMALLOC_TRACK)
unset(MIMALLOC_CFLAGS)
