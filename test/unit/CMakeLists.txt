add_compile_flags("C;CXX" "-Wno-unused")
file(GLOB all_sources *.c *.m *.mm)
set_source_files_compile_flags(${all_sources})

include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_BINARY_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/lib)
include_directories(${CMAKE_SOURCE_DIR}/third_party)
add_executable(rlist.test rlist.c test.c ${CMAKE_SOURCE_DIR}/src/lib/salad/rlist.c)

add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/src/port-uri.cc
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	COMMAND ragel -G2 src/port-uri.rl -o src/port-uri.cc
	DEPENDS ${CMAKE_SOURCE_DIR}/src/port-uri.rl)
add_executable(port-uri.test port-uri.cc test.c ${CMAKE_SOURCE_DIR}/src/port-uri.cc)
add_executable(fiob.test test.c fiob.c ${CMAKE_SOURCE_DIR}/src/fiob.c)
add_executable(queue.test queue.c)
add_executable(mhash.test mhash.c)
add_executable(rope_basic.test rope_basic.c ${CMAKE_SOURCE_DIR}/src/lib/salad/rope.c)
add_executable(rope_avl.test rope_avl.c ${CMAKE_SOURCE_DIR}/src/lib/salad/rope.c)
add_executable(rope_stress.test rope_stress.c ${CMAKE_SOURCE_DIR}/src/lib/salad/rope.c)
add_executable(rope.test rope.c ${CMAKE_SOURCE_DIR}/src/lib/salad/rope.c)
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
add_executable(lf_lifo.test lf_lifo.c)
add_executable(slab_arena.test slab_arena.c)
target_link_libraries(slab_arena.test small)
add_executable(arena_mt.test arena_mt.c)
target_link_libraries(arena_mt.test small pthread)
add_executable(bps_tree.test bps_tree.cc ${CMAKE_SOURCE_DIR}/third_party/qsort_arg.c)
target_link_libraries(bps_tree.test small)
add_executable(bps_tree_itr.test bps_tree_itr.cc ${CMAKE_SOURCE_DIR}/third_party/qsort_arg.c)
target_link_libraries(bps_tree_itr.test small)
add_executable(matras.test matras.cc)
target_link_libraries(matras.test small)
add_executable(vclock.test vclock.cc test.c
    ${CMAKE_SOURCE_DIR}/src/box/vclock.c)
target_link_libraries(vclock.test core small
    ${LIBEV_LIBRARIES} ${LIBEIO_LIBRARIES} ${LIBCORO_LIBRARIES})

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

add_executable(scramble.test scramble.c
    ${CMAKE_SOURCE_DIR}/src/scramble.c
    ${CMAKE_SOURCE_DIR}/third_party/sha1.c
    ${CMAKE_SOURCE_DIR}/third_party/base64.c
    ${CMAKE_SOURCE_DIR}/src/random.c)
