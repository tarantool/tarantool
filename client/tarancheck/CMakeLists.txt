project(tnt_checksum)

set (util_checksum "tarancheck")
set (util_checksum_sources tc_main.c tc_options.c tc_config.c tc_space.c tc_file.c tc_generate.c tc_verify.c)
set (util_checksum_libs tntrpl tntnet tnt gopt)

set_source_files_properties(${CMAKE_SOURCE_DIR}/cfg/tarantool_box_cfg.c
    ${CMAKE_SOURCE_DIR}/cfg/prscfg.c PROPERTIES COMPILE_FLAGS
    "-Wno-unused-parameter"
    GENERATED True)

set_source_files_properties(${util_checksum_sources} PROPERTIES OBJECT_DEPENDS
		            ${CMAKE_SOURCE_DIR}/cfg/tarantool_box_cfg.h)

list(APPEND util_checksum_sources
    ${CMAKE_SOURCE_DIR}/cfg/tarantool_box_cfg.c
    ${CMAKE_SOURCE_DIR}/cfg/prscfg.c)

list(APPEND util_checksum_sources
    ${CMAKE_SOURCE_DIR}/third_party/PMurHash.c)

set_source_files_compile_flags(
    ${util_checksum_sources})
add_executable(${util_checksum} ${util_checksum_sources})


target_link_libraries (${util_checksum} ${util_checksum_libs})

install (TARGETS ${util_checksum} DESTINATION bin)
