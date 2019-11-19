# A macro to build the bundled libcares
macro(ares_build)
    set(ARES_SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/c-ares)
    set(ARES_BINARY_DIR ${PROJECT_BINARY_DIR}/build/ares/work)
    set(ARES_INSTALL_DIR ${PROJECT_BINARY_DIR}/build/ares/dest)

    # See BuildLibCURL.cmake for details.
    set(ARES_CFLAGS "")
    if (TARGET_OS_DARWIN AND NOT "${CMAKE_OSX_SYSROOT}" STREQUAL "")
        set(ARES_CFLAGS "${ARES_CFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
    endif()

    set(ARES_CMAKE_FLAGS "-DCARES_STATIC=ON")
    list(APPEND ARES_CMAKE_FLAGS "-DCARES_SHARED=OFF")
    list(APPEND ARES_CMAKE_FLAGS "-DCARES_BUILD_TOOLS=OFF")
    # We build both static and shared versions of curl, so ares
    # has to be built with -fPIC for the shared version.
    list(APPEND ARES_CMAKE_FLAGS "-DCARES_STATIC_PIC=ON")
    # Even though we set the external project's install dir
    # below, we still need to pass the corresponding install
    # prefix via cmake arguments.
    list(APPEND ARES_CMAKE_FLAGS "-DCMAKE_INSTALL_PREFIX=${ARES_INSTALL_DIR}")
    # The default values for the options below are not always
    # "./lib", "./bin"  and "./include", while curl expects them
    # to be.
    list(APPEND ARES_CMAKE_FLAGS "-DCMAKE_INSTALL_LIBDIR=lib")
    list(APPEND ARES_CMAKE_FLAGS "-DCMAKE_INSTALL_INCLUDEDIR=include")
    list(APPEND ARES_CMAKE_FLAGS "-DCMAKE_INSTALL_BINDIR=bin")

    list(APPEND ARES_CMAKE_FLAGS "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
    list(APPEND ARES_CMAKE_FLAGS "-DCMAKE_LINKER=${CMAKE_LINKER}")
    list(APPEND ARES_CMAKE_FLAGS "-DCMAKE_AR=${CMAKE_AR}")
    list(APPEND ARES_CMAKE_FLAGS "-DCMAKE_RANLIB=${CMAKE_RANLIB}")
    list(APPEND ARES_CMAKE_FLAGS "-DCMAKE_NM=${CMAKE_NM}")
    list(APPEND ARES_CMAKE_FLAGS "-DCMAKE_STRIP=${CMAKE_STRIP}")
    list(APPEND ARES_CMAKE_FLAGS "-DCMAKE_C_FLAGS=${ARES_CFLAGS}")
    # In hardened mode, which enables -fPIE by default,
    # the cmake checks don't work without -fPIC.
    list(APPEND ARES_CMAKE_FLAGS "-DCMAKE_REQUIRED_FLAGS=-fPIC")

    include(ExternalProject)
    ExternalProject_Add(
        bundled-ares-project
        SOURCE_DIR ${ARES_SOURCE_DIR}
        INSTALL_DIR ${ARES_INSTALL_DIR}
        DOWNLOAD_DIR ${ARES_BINARY_DIR}
        TMP_DIR ${ARES_BINARY_DIR}/tmp
        STAMP_DIR ${ARES_BINARY_DIR}/stamp
        BINARY_DIR ${ARES_BINARY_DIR}
        CMAKE_ARGS ${ARES_CMAKE_FLAGS})

    add_library(bundled-ares STATIC IMPORTED GLOBAL)
    set_target_properties(bundled-ares PROPERTIES IMPORTED_LOCATION
        ${ARES_INSTALL_DIR}/lib/libcares.a)
    add_dependencies(bundled-ares bundled-ares-project)
    set(ARES_LIBRARIES bundled-ares)

    unset(ARES_CMAKE_FLAGS)
    unset(ARES_CFLAGS)
    unset(ARES_BINARY_DIR)
    unset(ARES_SOURCE_DIR)
endmacro(ares_build)
