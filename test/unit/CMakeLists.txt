add_compile_flags("C;CXX" "-Wno-unused")
file(GLOB all_sources *.c *.cc)
set_source_files_compile_flags(${all_sources})

include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_BINARY_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/lib)
include_directories(${CMAKE_SOURCE_DIR}/third_party)
add_executable(rlist.test rlist.c unit.c)
add_executable(stailq.test stailq.c unit.c)
add_executable(uri.test uri.c unit.c ${CMAKE_SOURCE_DIR}/src/uri.c)
add_executable(fiob.test unit.c fiob.c ${CMAKE_SOURCE_DIR}/src/fiob.c)
add_executable(queue.test queue.c)
add_executable(mhash.test mhash.c)
add_executable(mhash_bytemap.test mhash_bytemap.c)
add_executable(rope_basic.test rope_basic.c ${CMAKE_SOURCE_DIR}/src/lib/salad/rope.c)
add_executable(rope_avl.test rope_avl.c ${CMAKE_SOURCE_DIR}/src/lib/salad/rope.c)
add_executable(rope_stress.test rope_stress.c ${CMAKE_SOURCE_DIR}/src/lib/salad/rope.c)
add_executable(rope.test rope.c ${CMAKE_SOURCE_DIR}/src/lib/salad/rope.c)
add_executable(bit.test bit.c bit.c)
add_executable(int96.test int96.cc)
target_link_libraries(bit.test bit)
add_executable(bitset_basic.test bitset_basic.c)
target_link_libraries(bitset_basic.test bitset)
add_executable(bitset_iterator.test bitset_iterator.c)
target_link_libraries(bitset_iterator.test bitset)
add_executable(bitset_index.test bitset_index.c)
target_link_libraries(bitset_index.test bitset)
add_executable(base64.test base64.c ${CMAKE_SOURCE_DIR}/third_party/base64.c)

add_executable(bps_tree.test bps_tree.cc)
target_link_libraries(bps_tree.test small misc)
add_executable(bps_tree_itr.test bps_tree_itr.cc)
target_link_libraries(bps_tree_itr.test small misc)
add_executable(rtree.test rtree.cc)
target_link_libraries(rtree.test salad small)
add_executable(rtree_itr.test rtree_itr.cc)
target_link_libraries(rtree_itr.test salad small)
add_executable(rtree_multidim.test rtree_multidim.cc)
target_link_libraries(rtree_multidim.test salad small)
add_executable(light.test light.cc)
target_link_libraries(light.test small)
add_executable(vclock.test vclock.cc unit.c
    ${CMAKE_SOURCE_DIR}/src/box/vclock.c
    ${CMAKE_SOURCE_DIR}/src/box/errcode.c
    ${CMAKE_SOURCE_DIR}/src/box/error.cc
    ${CMAKE_SOURCE_DIR}/src/rmean.c)
target_link_libraries(vclock.test core small)
add_executable(xrow.test xrow.cc unit.c
    ${CMAKE_SOURCE_DIR}/src/box/xrow.cc
    ${CMAKE_SOURCE_DIR}/src/box/vclock.c
    ${CMAKE_SOURCE_DIR}/src/box/iproto_constants.c
    ${CMAKE_SOURCE_DIR}/src/box/errcode.c
    ${CMAKE_SOURCE_DIR}/src/box/error.cc)
target_link_libraries(xrow.test server misc msgpuck)

add_executable(fiber.test fiber.cc unit.c)
target_link_libraries(fiber.test core)

add_executable(fiber_stress.test fiber_stress.cc)
target_link_libraries(fiber_stress.test core)

add_executable(ipc.test ipc.cc unit.c ${CMAKE_SOURCE_DIR}/src/ipc.c)
target_link_libraries(ipc.test core)

add_executable(ipc_stress.test ipc_stress.cc ${CMAKE_SOURCE_DIR}/src/ipc.c)
target_link_libraries(ipc_stress.test core)

add_executable(coio.test coio.cc unit.c
        ${CMAKE_SOURCE_DIR}/src/sio.cc
        ${CMAKE_SOURCE_DIR}/src/evio.cc
        ${CMAKE_SOURCE_DIR}/src/coio.cc
        ${CMAKE_SOURCE_DIR}/src/coeio.c
        ${CMAKE_SOURCE_DIR}/src/uri.c
        ${CMAKE_SOURCE_DIR}/src/fio.c
        ${CMAKE_SOURCE_DIR}/src/iobuf.cc)
target_link_libraries(coio.test core eio bit)

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

add_executable(guava.test guava.c)
target_link_libraries(guava.test salad)

add_executable(find_path.test find_path.c
    ${CMAKE_SOURCE_DIR}/src/find_path.c
)

add_executable(reflection_c.test reflection_c.c unit.c
    ${CMAKE_SOURCE_DIR}/src/reflection.c)
add_executable(reflection_cxx.test reflection_cxx.cc unit.c
    ${CMAKE_SOURCE_DIR}/src/reflection.c)
add_executable(csv.test csv.c
    ${CMAKE_SOURCE_DIR}/src/lib/csv/csv.c
)

add_executable(rmean.test rmean.cc unit.c
        ${CMAKE_SOURCE_DIR}/src/rmean.c)
target_link_libraries(rmean.test core)

add_executable(say.test say.c unit.c)
target_link_libraries(say.test core)
