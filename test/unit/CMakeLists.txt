add_compile_flags("C;CXX" "-Wno-unused" "-Wno-unused-result")
if(CC_HAS_WNO_TAUTOLOGICAL_COMPARE)
    add_compile_flags("C;CXX" "-Wno-tautological-compare")
endif()
file(GLOB all_sources *.c *.cc)
set_source_files_compile_flags(${all_sources})

if(NOT TARGET_OS_OPENBSD)
    set(LIB_DL "dl")
endif()

include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_BINARY_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/box)
include_directories(${CMAKE_SOURCE_DIR}/third_party)
include_directories(${ICU_INCLUDE_DIRS})
include_directories(${CURL_INCLUDE_DIRS})

add_library(unit STATIC unit.c)

add_executable(heap.test heap.c)
target_link_libraries(heap.test unit)
add_executable(heap_iterator.test heap_iterator.c)
target_link_libraries(heap_iterator.test unit)
add_executable(stailq.test stailq.c)
target_link_libraries(stailq.test unit)
add_executable(uri.test uri.c unit.c)
target_link_libraries(uri.test uri unit)
add_executable(queue.test queue.c)
add_executable(mhash.test mhash.c)
target_link_libraries(mhash.test unit)
add_executable(mhash_bytemap.test mhash_bytemap.c)
target_link_libraries(mhash_bytemap.test unit)
add_executable(rope_basic.test rope_basic.c)
target_link_libraries(rope_basic.test salad)
add_executable(gh-5788-rope-insert-oom.test gh-5788-rope-insert-oom.c)
target_link_libraries(gh-5788-rope-insert-oom.test salad unit)
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
add_executable(uuid.test uuid.c core_test_utils.c)
target_link_libraries(uuid.test uuid unit)
add_executable(random.test random.c core_test_utils.c)
target_link_libraries(random.test core unit)

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
add_executable(xrow.test xrow.cc core_test_utils.c)
target_link_libraries(xrow.test xrow unit)
add_executable(decimal.test decimal.c)
target_link_libraries(decimal.test core unit)
add_executable(mp_error.test mp_error.cc core_test_utils.c)
target_link_libraries(mp_error.test box_error core unit)
add_executable(fiber.test fiber.cc core_test_utils.c)
target_link_libraries(fiber.test core unit)
add_executable(fiber_stack.test fiber_stack.c core_test_utils.c)
target_link_libraries(fiber_stack.test core unit)

if (NOT ENABLE_GCOV)
    # This test is known to be broken with GCOV
    add_executable(guard.test guard.cc core_test_utils.c)
    target_link_libraries(guard.test core unit)
endif ()

add_executable(fiber_stress.test fiber_stress.cc core_test_utils.c)
target_link_libraries(fiber_stress.test core)

add_executable(fiber_cond.test fiber_cond.c unit.c core_test_utils.c)
target_link_libraries(fiber_cond.test core)

add_executable(fiber_channel.test fiber_channel.cc unit.c core_test_utils.c)
target_link_libraries(fiber_channel.test core)

add_executable(fiber_channel_stress.test fiber_channel_stress.cc core_test_utils.c)
target_link_libraries(fiber_channel_stress.test core)

add_executable(cbus_stress.test cbus_stress.c core_test_utils.c)
target_link_libraries(cbus_stress.test core stat)

add_executable(cbus.test cbus.c core_test_utils.c)
target_link_libraries(cbus.test core unit stat)

include(CheckSymbolExists)
check_symbol_exists(__GLIBC__ features.h GLIBC_USED)
if (GLIBC_USED)
    add_executable(cbus_hang.test cbus_hang.c core_test_utils.c)
    target_link_libraries(cbus_hang.test core unit stat)
endif ()

add_executable(coio.test coio.cc core_test_utils.c)
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

add_executable(scramble.test scramble.c core_test_utils.c)
target_link_libraries(scramble.test scramble)

add_executable(guava.test guava.c)
target_link_libraries(guava.test salad small)

add_executable(crc32.test crc32.c)
target_link_libraries(crc32.test unit crc32)

add_executable(find_path.test find_path.c
    ${CMAKE_SOURCE_DIR}/src/find_path.c
)

add_executable(reflection_c.test reflection_c.c unit.c
    ${CMAKE_SOURCE_DIR}/src/lib/core/reflection.c)
add_executable(reflection_cxx.test reflection_cxx.cc unit.c
    ${CMAKE_SOURCE_DIR}/src/lib/core/reflection.c)
add_executable(csv.test csv.c)
target_link_libraries(csv.test csv)

add_executable(json.test json.c)
target_link_libraries(json.test json unit ${ICU_LIBRARIES})

add_executable(rmean.test rmean.cc core_test_utils.c)
target_link_libraries(rmean.test stat unit)
add_executable(histogram.test histogram.c core_test_utils.c)
target_link_libraries(histogram.test stat unit)
add_executable(ratelimit.test ratelimit.c)
target_link_libraries(ratelimit.test unit)
add_executable(luaT_tuple_new.test luaT_tuple_new.c)
target_link_libraries(luaT_tuple_new.test unit box server core misc
    ${CURL_LIBRARIES} ${LIBYAML_LIBRARIES} ${READLINE_LIBRARIES}
    ${ICU_LIBRARIES} ${LUAJIT_LIBRARIES})
add_executable(luaL_iterator.test luaL_iterator.c)
target_link_libraries(luaL_iterator.test unit server coll core misc
    ${CURL_LIBRARIES} ${LIBYAML_LIBRARIES} ${READLINE_LIBRARIES}
    ${ICU_LIBRARIES} ${LUAJIT_LIBRARIES} ${LIB_DL})

add_executable(say.test say.c core_test_utils.c)
target_link_libraries(say.test core unit)

set(ITERATOR_TEST_SOURCES
    vy_iterators_helper.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_stmt.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_upsert.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_history.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_mem.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_cache.c)
set(ITERATOR_TEST_LIBS core tuple xrow unit)

add_executable(vy_mem.test vy_mem.c ${ITERATOR_TEST_SOURCES} core_test_utils.c)
target_link_libraries(vy_mem.test ${ITERATOR_TEST_LIBS} ${LIB_DL})

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
    core_test_utils.c
)
target_link_libraries(vy_point_lookup.test core tuple xrow xlog unit ${LIB_DL})

add_executable(column_mask.test
    column_mask.c
    core_test_utils.c)
target_link_libraries(column_mask.test tuple unit)

add_executable(vy_write_iterator.test
    vy_write_iterator.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_run.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_upsert.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_write_iterator.c
    ${ITERATOR_TEST_SOURCES}
    core_test_utils.c
)
target_link_libraries(vy_write_iterator.test xlog ${ITERATOR_TEST_LIBS} ${LIB_DL})

add_executable(vy_cache.test vy_cache.c ${ITERATOR_TEST_SOURCES} core_test_utils.c)
target_link_libraries(vy_cache.test ${ITERATOR_TEST_LIBS} ${LIB_DL})

add_executable(coll.test coll.cpp core_test_utils.c)
target_link_libraries(coll.test coll unit misc ${LIB_DL})

add_executable(tuple_bigref.test tuple_bigref.c core_test_utils.c)
target_link_libraries(tuple_bigref.test tuple unit)

add_executable(tuple_uint32_overflow.test tuple_uint32_overflow.c core_test_utils.c)
target_link_libraries(tuple_uint32_overflow.test tuple unit)

add_executable(checkpoint_schedule.test
    checkpoint_schedule.c
    ${PROJECT_SOURCE_DIR}/src/box/checkpoint_schedule.c
)
target_link_libraries(checkpoint_schedule.test m unit)

add_executable(sio.test sio.c core_test_utils.c)
target_link_libraries(sio.test unit core)

add_executable(crypto.test crypto.c core_test_utils.c)
target_link_libraries(crypto.test crypto unit)

add_executable(swim.test swim.c swim_test_transport.c swim_test_ev.c
               swim_test_utils.c ${PROJECT_SOURCE_DIR}/src/version.c core_test_utils.c)
target_link_libraries(swim.test unit fakesys swim)

add_executable(swim_proto.test swim_proto.c swim_test_transport.c swim_test_ev.c
               swim_test_utils.c ${PROJECT_SOURCE_DIR}/src/version.c core_test_utils.c)
target_link_libraries(swim_proto.test unit fakesys swim)

add_executable(swim_errinj.test swim_errinj.c swim_test_transport.c
               swim_test_ev.c swim_test_utils.c
               ${PROJECT_SOURCE_DIR}/src/version.c core_test_utils.c)
target_link_libraries(swim_errinj.test unit fakesys swim)

add_executable(merger.test merger.test.c)
target_link_libraries(merger.test unit core box)

add_executable(snap_quorum_delay.test snap_quorum_delay.cc)
target_link_libraries(snap_quorum_delay.test box core unit)

add_executable(raft.test raft.c raft_test_utils.c core_test_utils.c)
target_link_libraries(raft.test vclock unit fakesys raft_algo)

#
# Client for popen.test
add_executable(popen-child popen-child.c)

add_executable(popen.test popen.c core_test_utils.c)
target_link_libraries(popen.test misc unit core)

add_executable(serializer.test serializer.c)
target_link_libraries(serializer.test unit box ${LUAJIT_LIBRARIES})
