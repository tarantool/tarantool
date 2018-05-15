add_compile_flags("C;CXX" "-Wno-unused" "-Wno-unused-result")
if(CC_HAS_WNO_TAUTOLOGICAL_COMPARE)
    add_compile_flags("C;CXX" "-Wno-tautological-compare")
endif()
file(GLOB all_sources *.c *.cc)
set_source_files_compile_flags(${all_sources})

include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_BINARY_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/box)
include_directories(${CMAKE_SOURCE_DIR}/third_party)
include_directories(${ICU_INCLUDE_DIRS})

add_library(unit STATIC unit.c)

add_executable(heap.test heap.c)
target_link_libraries(heap.test unit)
add_executable(heap_iterator.test heap_iterator.c)
target_link_libraries(heap_iterator.test unit)
add_executable(rlist.test rlist.c)
target_link_libraries(rlist.test unit)
add_executable(stailq.test stailq.c)
target_link_libraries(stailq.test unit)
add_executable(uri.test uri.c unit.c)
target_link_libraries(uri.test uri unit)
add_executable(queue.test queue.c)
add_executable(mhash.test mhash.c)
add_executable(mhash_bytemap.test mhash_bytemap.c)
add_executable(rope_basic.test rope_basic.c)
target_link_libraries(rope_basic.test salad)
add_executable(rope_avl.test rope_avl.c)
target_link_libraries(rope_avl.test salad)
add_executable(rope_stress.test rope_stress.c)
target_link_libraries(rope_stress.test salad)
add_executable(rope.test rope.c)
target_link_libraries(rope.test salad)
add_executable(int96.test int96.cc)
add_executable(bit.test bit.c bit.c)
target_link_libraries(bit.test bit)
add_executable(bitset_basic.test bitset_basic.c)
target_link_libraries(bitset_basic.test bitset)
add_executable(bitset_iterator.test bitset_iterator.c)
target_link_libraries(bitset_iterator.test bitset)
add_executable(bitset_index.test bitset_index.c)
target_link_libraries(bitset_index.test bitset)
add_executable(base64.test base64.c)
target_link_libraries(base64.test misc unit)
add_executable(uuid.test uuid.c)
target_link_libraries(uuid.test uuid unit)

add_executable(bps_tree.test bps_tree.cc)
target_link_libraries(bps_tree.test small misc)
add_executable(bps_tree_iterator.test bps_tree_iterator.cc)
target_link_libraries(bps_tree_iterator.test small misc)
add_executable(rtree.test rtree.cc)
target_link_libraries(rtree.test salad small)
add_executable(rtree_iterator.test rtree_iterator.cc)
target_link_libraries(rtree_iterator.test salad small)
add_executable(rtree_multidim.test rtree_multidim.cc)
target_link_libraries(rtree_multidim.test salad small)
add_executable(light.test light.cc)
target_link_libraries(light.test small)
add_executable(bloom.test bloom.cc)
target_link_libraries(bloom.test salad)
add_executable(vclock.test vclock.cc)
target_link_libraries(vclock.test vclock unit)
add_executable(xrow.test xrow.cc)
target_link_libraries(xrow.test xrow unit)

add_executable(fiber.test fiber.cc)
set_source_files_properties(fiber.cc PROPERTIES COMPILE_FLAGS -O0)
target_link_libraries(fiber.test core unit)

if (NOT ENABLE_GCOV)
    # This test is known to be broken with GCOV
    add_executable(guard.test guard.cc)
    target_link_libraries(guard.test core unit)
endif ()

add_executable(fiber_stress.test fiber_stress.cc)
target_link_libraries(fiber_stress.test core)

add_executable(fiber_cond.test fiber_cond.c unit.c)
target_link_libraries(fiber_cond.test core)

add_executable(fiber_channel.test fiber_channel.cc unit.c)
target_link_libraries(fiber_channel.test core)

add_executable(fiber_channel_stress.test fiber_channel_stress.cc)
target_link_libraries(fiber_channel_stress.test core)

add_executable(cbus_stress.test cbus_stress.c)
target_link_libraries(cbus_stress.test core stat)

add_executable(cbus.test cbus.c)
target_link_libraries(cbus.test core unit stat)

add_executable(coio.test coio.cc)
target_link_libraries(coio.test core eio bit uri unit)

if (ENABLE_BUNDLED_MSGPUCK)
    set(MSGPUCK_DIR ${PROJECT_SOURCE_DIR}/src/lib/msgpuck/)
    add_executable(msgpack.test
        ${MSGPUCK_DIR}/test/msgpuck.c
        ${MSGPUCK_DIR}/test/test.c)

    set_source_files_properties(
        ${MSGPUCK_DIR}/test/msgpuck.c
        ${MSGPUCK_DIR}/test/test.c
        PROPERTIES COMPILE_FLAGS "-I${MSGPUCK_DIR}/test")

    target_link_libraries(msgpack.test ${MSGPUCK_LIBRARIES})
endif ()

add_executable(scramble.test scramble.c)
target_link_libraries(scramble.test scramble)

add_executable(guava.test guava.c)
target_link_libraries(guava.test salad small)

add_executable(find_path.test find_path.c
    ${CMAKE_SOURCE_DIR}/src/find_path.c
)

add_executable(reflection_c.test reflection_c.c unit.c
    ${CMAKE_SOURCE_DIR}/src/reflection.c)
add_executable(reflection_cxx.test reflection_cxx.cc unit.c
    ${CMAKE_SOURCE_DIR}/src/reflection.c)
add_executable(csv.test csv.c)
target_link_libraries(csv.test csv)

add_executable(json_path.test json_path.c)
target_link_libraries(json_path.test json_path unit ${ICU_LIBRARIES})

add_executable(rmean.test rmean.cc)
target_link_libraries(rmean.test stat unit)
add_executable(histogram.test histogram.c)
target_link_libraries(histogram.test stat unit)

add_executable(say.test say.c)
target_link_libraries(say.test core unit)

set(ITERATOR_TEST_SOURCES
    vy_iterators_helper.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_stmt.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_upsert.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_history.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_mem.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_cache.c)
set(ITERATOR_TEST_LIBS core tuple xrow unit)

add_executable(vy_mem.test vy_mem.c ${ITERATOR_TEST_SOURCES})
target_link_libraries(vy_mem.test ${ITERATOR_TEST_LIBS})

add_executable(vy_point_lookup.test
    vy_point_lookup.c
    vy_iterators_helper.c
    vy_log_stub.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_point_lookup.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_write_iterator.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_stmt.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_mem.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_run.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_range.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_tx.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_read_set.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_upsert.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_history.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_lsm.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_cache.c
    ${PROJECT_SOURCE_DIR}/src/box/index_def.c
    ${PROJECT_SOURCE_DIR}/src/box/schema_def.c
    ${PROJECT_SOURCE_DIR}/src/box/identifier.c
)
target_link_libraries(vy_point_lookup.test core tuple xrow xlog unit)

add_executable(column_mask.test
    column_mask.c)
target_link_libraries(column_mask.test tuple unit)

add_executable(vy_write_iterator.test
    vy_write_iterator.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_run.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_upsert.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_write_iterator.c
    ${ITERATOR_TEST_SOURCES}
)
target_link_libraries(vy_write_iterator.test xlog ${ITERATOR_TEST_LIBS})

add_executable(vy_cache.test vy_cache.c ${ITERATOR_TEST_SOURCES})
target_link_libraries(vy_cache.test ${ITERATOR_TEST_LIBS})

add_executable(coll.test coll.cpp)
target_link_libraries(coll.test core unit ${ICU_LIBRARIES} misc)
