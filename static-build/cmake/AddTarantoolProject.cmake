# Get install directories of built libraries for building
# tarantool with custom CMAKE_PREFIX_PATH
foreach(proj IN LISTS TARANTOOL_DEPENDS)
    ExternalProject_Get_Property(${proj} install_dir)
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH}:${install_dir})
    message(STATUS "Add external project ${proj} in ${install_dir}")
endforeach()

ExternalProject_Add(tarantool
    DEPENDS ${TARANTOOL_DEPENDS}
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..
    LIST_SEPARATOR :
    CMAKE_ARGS
        # Override LOCALSTATEDIR to avoid cmake "special" cases:
        # https://cmake.org/cmake/help/v3.4/module/GNUInstallDirs.html#special-cases
        -DCMAKE_INSTALL_LOCALSTATEDIR=<INSTALL_DIR>/var
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
        # Ensure that no external dependencies are dynamically linked.
        -DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=FALSE
        -DOPENSSL_USE_STATIC_LIBS=TRUE
        -DBUILD_STATIC=TRUE
        -DENABLE_DIST=TRUE
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
