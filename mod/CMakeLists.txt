
function(tarantool_module mod)
  set (module_sources ${ARGV})
  list(REMOVE_ITEM module_sources ${mod})
  set_source_files_properties(
      ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.c
      PROPERTIES COMPILE_FLAGS "-Wno-unused" GENERATED True)
  add_executable(tarantool_${mod}
      ${module_sources}
      ${CMAKE_SOURCE_DIR}/cfg/tarantool_${mod}_cfg.c)

  if (mod STREQUAL "feeder")
    set (STORAGE_OR_UTILITY "UTILITY")
  else()
    set (STORAGE_OR_UTILITY "STORAGE")
  endif()

  add_library(lt${mod} STATIC ${recompiled_core_sources})
  set_target_properties(lt${mod} PROPERTIES COMPILE_FLAGS
    "${core_cflags} -D${STORAGE_OR_UTILITY} -DTARANTOOL_CONFIG='<cfg/tarantool_${mod}_cfg.h>'")
  add_dependencies(lt${mod} generate_headers generate_admin_c)

  target_link_libraries(tarantool_${mod} lt${mod} ${common_libraries})
  set_target_properties(tarantool_${mod} PROPERTIES COMPILE_FLAGS
      "${core_cflags}")

endfunction()

foreach (module ${TARANTOOL_MODULES})
  add_subdirectory(${module})
endforeach()
