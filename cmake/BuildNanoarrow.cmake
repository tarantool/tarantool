# A macro to build the bundled nanoarrow library
macro(nanoarrow_build)
    set(NANOARROW_SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/arrow/nanoarrow/)
    set(NANOARROW_INSTALL_DIR ${PROJECT_BINARY_DIR}/build/nanoarrow/)
    set(NANOARROW_INCLUDE_DIR ${NANOARROW_INSTALL_DIR}/include/)

    set(NANOARROW_CORE_LIBRARY ${NANOARROW_INSTALL_DIR}/lib/libnanoarrow.a)
    set(NANOARROW_IPC_LIBRARY ${NANOARROW_INSTALL_DIR}/lib/libnanoarrow_ipc.a)
    set(FLATCCRT_LIBRARY ${NANOARROW_INSTALL_DIR}/lib/libflatccrt.a)
    set(NANOARROW_LIBRARIES
        ${NANOARROW_CORE_LIBRARY} ${NANOARROW_IPC_LIBRARY} ${FLATCCRT_LIBRARY})

    set(NANOARROW_CFLAGS ${DEPENDENCY_CFLAGS})

    # Silence the "src/nanoarrow/ipc/decoder.c:1115:7: error: conversion from
    # `long unsigned int' to `int32_t' {aka `int'} may change value" warning
    # on ancient compilers, in particular GCC 4.8.5 from CentOS 7.
    set(NANOARROW_CFLAGS "${NANOARROW_CFLAGS} -Wno-error=conversion")

    list(APPEND NANOARROW_CMAKE_FLAGS "-DCMAKE_C_FLAGS=${NANOARROW_CFLAGS}")

    # Build nanoarrow IPC extension.
    list(APPEND NANOARROW_CMAKE_FLAGS "-DNANOARROW_IPC=ON")

    # Switch on the static build.
    list(APPEND NANOARROW_CMAKE_FLAGS "-DBUILD_STATIC_LIBS=ON")

    # Switch off the shared build.
    list(APPEND NANOARROW_CMAKE_FLAGS "-DBUILD_SHARED_LIBS=OFF")

    # Even though we set the external project's install dir
    # below, we still need to pass the corresponding install
    # prefix via cmake arguments.
    list(APPEND NANOARROW_CMAKE_FLAGS
        "-DCMAKE_INSTALL_PREFIX=${NANOARROW_INSTALL_DIR}")

    # Pass the same toolchain as is used to build tarantool itself,
    # because they can be incompatible.
    list(APPEND NANOARROW_CMAKE_FLAGS "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
    list(APPEND NANOARROW_CMAKE_FLAGS "-DCMAKE_LINKER=${CMAKE_LINKER}")
    list(APPEND NANOARROW_CMAKE_FLAGS "-DCMAKE_AR=${CMAKE_AR}")
    list(APPEND NANOARROW_CMAKE_FLAGS "-DCMAKE_RANLIB=${CMAKE_RANLIB}")
    list(APPEND NANOARROW_CMAKE_FLAGS "-DCMAKE_NM=${CMAKE_NM}")
    list(APPEND NANOARROW_CMAKE_FLAGS "-DCMAKE_STRIP=${CMAKE_STRIP}")
    list(APPEND NANOARROW_CMAKE_FLAGS "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")

    include(ExternalProject)
    ExternalProject_Add(bundled-nanoarrow-project
        PREFIX ${NANOARROW_INSTALL_DIR}
        SOURCE_DIR ${NANOARROW_SOURCE_DIR}
        CMAKE_ARGS ${NANOARROW_CMAKE_FLAGS}
        BUILD_BYPRODUCTS ${NANOARROW_LIBRARIES}
    )

    add_library(bundled-nanoarrow-core STATIC IMPORTED GLOBAL)
    set_target_properties(bundled-nanoarrow-core PROPERTIES IMPORTED_LOCATION
        ${NANOARROW_CORE_LIBRARY})
    add_dependencies(bundled-nanoarrow-core bundled-nanoarrow-project)

    add_library(bundled-nanoarrow-ipc STATIC IMPORTED GLOBAL)
    set_target_properties(bundled-nanoarrow-ipc PROPERTIES IMPORTED_LOCATION
        ${NANOARROW_IPC_LIBRARY})
    add_dependencies(bundled-nanoarrow-ipc bundled-nanoarrow-project)

    add_library(bundled-flatccrt STATIC IMPORTED GLOBAL)
    set_target_properties(bundled-flatccrt PROPERTIES IMPORTED_LOCATION
        ${FLATCCRT_LIBRARY})
    add_dependencies(bundled-flatccrt bundled-nanoarrow-project)

    add_custom_target(bundled-nanoarrow
        DEPENDS bundled-nanoarrow-core bundled-nanoarrow-ipc bundled-flatccrt)

    # Setup NANOARROW_INCLUDE_DIRS for global use.
    set(NANOARROW_INCLUDE_DIRS ${NANOARROW_INCLUDE_DIR})
endmacro()
