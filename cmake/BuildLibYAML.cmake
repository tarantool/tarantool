#
# A macro to build the bundled libyaml
macro(libyaml_build)
    set(LIBYAML_SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/libyaml)
    set(LIBYAML_INSTALL_DIR ${PROJECT_BINARY_DIR}/build/libyaml)
    set(LIBYAML_INCLUDE_DIR ${LIBYAML_INSTALL_DIR}/include)
    set(LIBYAML_LIBRARY ${LIBYAML_INSTALL_DIR}/lib/libyaml_static.a)

    set(LIBYAML_CMAKE_FLAGS
        "-DCMAKE_INSTALL_PREFIX=${LIBYAML_INSTALL_DIR}"
        "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
        "-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}"
    )

    if(DEFINED ICU_ROOT)
        list(APPEND LIBYAML_CMAKE_FLAGS "-DICU_ROOT=${ICU_ROOT}")
    endif()

    ExternalProject_Add(bundled-libyaml-project
        PREFIX ${LIBYAML_INSTALL_DIR}
        SOURCE_DIR ${LIBYAML_SOURCE_DIR}
        CMAKE_ARGS ${LIBYAML_CMAKE_FLAGS}
        BUILD_BYPRODUCTS ${LIBYAML_LIBRARY}
    )

    if(ENABLE_BUNDLED_ICU)
        add_dependencies(bundled-libyaml-project bundled-icu)
    endif()

    add_library(bundled-libyaml STATIC IMPORTED GLOBAL)
    set_target_properties(bundled-libyaml PROPERTIES IMPORTED_LOCATION
        ${LIBYAML_LIBRARY})
    add_dependencies(bundled-libyaml bundled-libyaml-project)

    set(LIBYAML_LIBRARIES ${LIBYAML_LIBRARY})
    set(LIBYAML_INCLUDE_DIRS ${LIBYAML_INCLUDE_DIR})

    message(STATUS "Using bundled libyaml")
endmacro(libyaml_build)
