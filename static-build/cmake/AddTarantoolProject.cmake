include(ExternalProject)

ExternalProject_Add(tarantool
    DEPENDS ${TARANTOOL_DEPENDS}
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..
    LIST_SEPARATOR :
    CMAKE_ARGS
        # Override LOCALSTATEDIR to avoid cmake "special" cases:
        # https://cmake.org/cmake/help/v3.4/module/GNUInstallDirs.html#special-cases
        -DCMAKE_INSTALL_LOCALSTATEDIR=<INSTALL_DIR>/var
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DOPENSSL_USE_STATIC_LIBS=TRUE
        -DBUILD_STATIC_WITH_BUNDLED_LIBS=TRUE
        -DENABLE_DIST=TRUE
        -DENABLE_BACKTRACE=TRUE
        -DENABLE_HARDENING=${ENABLE_HARDENING}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        ${CMAKE_TARANTOOL_ARGS}
    STEP_TARGETS build
    BUILD_COMMAND $(MAKE)
    BUILD_ALWAYS TRUE
)

ExternalProject_Get_Property(tarantool install_dir)
set(TARANTOOL_BINARY ${install_dir}/bin/tarantool)
