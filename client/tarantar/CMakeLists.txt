project(tnt_snap)

set (util_snap "tarantar")
set (util_snap_sources sha1.c main.c config.c space.c options.c indexate.c ref.c cursor.c snapshot.c update.cc compat.c)
set (util_snap_libs tntrpl tntnet tnt)

list(APPEND util_snap_sources
    ${CMAKE_SOURCE_DIR}/third_party/crc32.c
    ${CMAKE_SOURCE_DIR}/cfg/tarantool_box_cfg.c
    ${CMAKE_SOURCE_DIR}/cfg/prscfg.c
    ${CMAKE_SOURCE_DIR}/src/errcode.c
    ${CMAKE_SOURCE_DIR}/src/exception.cc
    ${CMAKE_SOURCE_DIR}/src/rope.c
    ${CMAKE_SOURCE_DIR}/src/box/tuple_update.cc )

set_source_files_properties(${CMAKE_SOURCE_DIR}/cfg/tarantool_box_cfg.c
     ${CMAKE_SOURCE_DIR}/cfg/prscfg.c PROPERTIES COMPILE_FLAGS
     "-Wno-unused-parameter"
     GENERATED True)

set_source_files_properties(${util_snap_sources} PROPERTIES OBJECT_DEPENDS
    ${CMAKE_SOURCE_DIR}/cfg/tarantool_box_cfg.h)

set_source_files_compile_flags(${util_snap_sources})
add_executable(${util_snap} ${util_snap_sources})

target_link_libraries (${util_snap} ${util_snap_libs} gopt small)

install (TARGETS ${util_snap} DESTINATION bin)
