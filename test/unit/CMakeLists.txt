file(GLOB all_sources *.c *.m *.mm)
set_source_files_compile_flags(${all_sources})

add_executable(rlist.test rlist.c test.c ${CMAKE_SOURCE_DIR}/src/rlist.c)
add_executable(queue.test queue.c)
add_executable(mhash.test mhash.c)
add_executable(rope_basic.test rope_basic.c ${CMAKE_SOURCE_DIR}/src/rope.c)
add_executable(rope_avl.test rope_avl.c ${CMAKE_SOURCE_DIR}/src/rope.c)
add_executable(rope_stress.test rope_stress.c ${CMAKE_SOURCE_DIR}/src/rope.c)
add_executable(rope.test rope.c ${CMAKE_SOURCE_DIR}/src/rope.c)
add_executable(bit.test bit.c bit.c)
target_link_libraries(bit.test bit)
add_executable(bitset_basic.test bitset_basic.c)
target_link_libraries(bitset_basic.test bitset)
add_executable(bitset_iterator.test bitset_iterator.c)
target_link_libraries(bitset_iterator.test bitset)
add_executable(bitset_index.test bitset_index.c)
target_link_libraries(bitset_index.test bitset)
add_executable(base64.test base64.c ${CMAKE_SOURCE_DIR}/third_party/base64.c)
add_executable(slab_cache.test slab_cache.c)
target_link_libraries(slab_cache.test small)
add_executable(region.test region.c)
target_link_libraries(region.test small)
add_executable(mempool.test mempool.c)
target_link_libraries(mempool.test small)
add_executable(small_alloc.test small_alloc.c)
target_link_libraries(small_alloc.test small)

set(MSGPUCK_DIR ${PROJECT_SOURCE_DIR}/src/lib/msgpuck/)
add_executable(msgpack.test
    ${MSGPUCK_DIR}/test/msgpuck.c
    ${MSGPUCK_DIR}/test/test.c)

set_source_files_properties(
    ${MSGPUCK_DIR}/test/msgpuck.c
    ${MSGPUCK_DIR}/test/test.c
    PROPERTIES COMPILE_FLAGS
    "-I${MSGPUCK_DIR} -I${MSGPUCK_DIR}/test")

target_link_libraries(msgpack.test msgpuck)
