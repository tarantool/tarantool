macro(libccdt_build)
    set(LIBCDT_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/third_party/c-dt/)
    set(LIBCDT_LIBRARIES cdt)

    add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/c-dt)
endmacro()
