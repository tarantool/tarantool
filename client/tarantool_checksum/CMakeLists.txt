project(tnt_checksum)

set (util_checksum "tarantool_checksum")
set (util_checksum_sources tc_main.c tc_options.c tc_config.c tc_space.c tc_file.c tc_generate.c tc_verify.c)
set (util_checksum_libs tntrpl tntnet tnt gopt)

set_source_files_properties(${CMAKE_SOURCE_DIR}/cfg/tarantool_box_cfg.c
                            ${CMAKE_SOURCE_DIR}/cfg/prscfg.c
		            PROPERTIES COMPILE_FLAGS "-Wno-unused" GENERATED True)

set_source_files_properties(${util_checksum_sources} PROPERTIES OBJECT_DEPENDS
		            ${CMAKE_SOURCE_DIR}/cfg/tarantool_box_cfg.h)

add_executable(${util_checksum} ${util_checksum_sources}
               ${CMAKE_SOURCE_DIR}/cfg/tarantool_box_cfg.c
               ${CMAKE_SOURCE_DIR}/cfg/prscfg.c)

set_target_properties(${util_checksum} PROPERTIES COMPILE_FLAGS "${core_cflags}")

target_link_libraries (${util_checksum} ${util_checksum_libs})

install (TARGETS ${util_checksum} DESTINATION bin)
