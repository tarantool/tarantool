macro(libccdt_build)
    set(LIBCDT_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/third_party/c-dt/)
    set(LIBCDT_LIBRARIES cdt)

    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/third_party/c-dt/build/)
    add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/c-dt
                     ${CMAKE_CURRENT_BINARY_DIR}/third_party/c-dt/build/)
    set_target_properties(cdt
        PROPERTIES COMPILE_FLAGS
            "${DEPENDENCY_CFLAGS} -DDT_NAMESPACE=tnt_ -DDT_PARSE_ISO_YEAR0 -DDT_PARSE_ISO_TNT"
    )
    add_definitions("-DDT_NAMESPACE=tnt_")
endmacro()
