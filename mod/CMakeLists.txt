
function(tarantool_module mod)
    set (module_sources ${ARGN})
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.c
        PROPERTIES COMPILE_FLAGS "-Wno-unused" GENERATED True)
    add_executable(tarantool_${mod}
        ${module_sources}
        ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.c)

    set_source_files_properties(${recompiled_sources}
        PROPERTIES OBJECT_DEPENDS
        ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.h)

    add_library(lt${mod} STATIC ${recompiled_sources})
    set_target_properties(lt${mod} PROPERTIES COMPILE_FLAGS
        "${core_cflags} ${GCOV_C_FLAGS} -DTARANTOOL_CONFIG='<cfg/tarantool_${mod}_cfg.h>'")
    add_dependencies(lt${mod} generate_headers generate_admin_m build_bundled_libs)

    target_link_libraries(tarantool_${mod} lt${mod} ${GCOV_LDFLAGS} ${common_libraries})
    set_target_properties(tarantool_${mod} PROPERTIES COMPILE_FLAGS
        "${core_cflags} ${GCOV_C_FLAGS}")

    if (ENABLE_STATIC)
        set_target_properties(tarantool_${mod} PROPERTIES
            LINK_SEARCH_END_STATIC ON)
    endif()

    if (module_link_flags)
        set_target_properties(tarantool_${mod} PROPERTIES
            LINK_FLAGS ${module_link_flags})
    endif()
    install (TARGETS tarantool_${mod} DESTINATION bin)
endfunction()

foreach (module ${TARANTOOL_MODULES})
add_subdirectory(${module})
endforeach()
