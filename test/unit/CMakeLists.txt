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
include_directories(${PROJECT_SOURCE_DIR}/third_party)
include_directories(${ICU_INCLUDE_DIRS})
include_directories(${CURL_INCLUDE_DIRS})

include_directories(${EXTRA_CORE_INCLUDE_DIRS})

include(CMakeParseArguments)

function(create_unit_test)
  cmake_parse_arguments(
    UNIT
    ""
    "PREFIX"
    "SOURCES;LIBRARIES"
    ${ARGN}
  )
  message(STATUS "Creating unit test ${UNIT_PREFIX}.test")
  add_executable(${UNIT_PREFIX}.test ${UNIT_SOURCES})
  target_link_libraries(${UNIT_PREFIX}.test ${UNIT_LIBRARIES})
  set(UNIT_TEST_TARGETS "${UNIT_TEST_TARGETS} ${UNIT_PREFIX}.test" PARENT_SCOPE)
endfunction()

add_library(unit STATIC unit.c)

create_unit_test(PREFIX heap
                 SOURCES heap.c
                 LIBRARIES unit
)
create_unit_test(PREFIX heap_iterator
                 SOURCES heap_iterator.c
                 LIBRARIES unit
)
create_unit_test(PREFIX stailq
                 SOURCES stailq.c
                 LIBRARIES unit
)
create_unit_test(PREFIX uri_parser
                 SOURCES uri_parser.c unit.c
                 LIBRARIES uri unit
)
create_unit_test(PREFIX uri
                 SOURCES uri.c unit.c
                 LIBRARIES uri unit
)
create_unit_test(PREFIX queue
                 SOURCES queue.c
                 LIBRARIES
)
create_unit_test(PREFIX mhash
                 SOURCES mhash.c
                 LIBRARIES unit
)
create_unit_test(PREFIX mhash_bytemap
                 SOURCES mhash_bytemap.c
                 LIBRARIES unit
)
create_unit_test(PREFIX rope_basic
                 SOURCES rope_basic.c
                 LIBRARIES salad
)
create_unit_test(PREFIX gh-5788-rope-insert-oom
                 SOURCES gh-5788-rope-insert-oom.c
                 LIBRARIES salad unit
)
create_unit_test(PREFIX rope_avl
                 SOURCES rope_avl.c
                 LIBRARIES salad
)
create_unit_test(PREFIX rope_stress
                 SOURCES rope_stress.c
                 LIBRARIES salad
)
create_unit_test(PREFIX rope
                 SOURCES rope.c
                 LIBRARIES salad
)
create_unit_test(PREFIX int96
                 SOURCES int96.cc
                 LIBRARIES
)
create_unit_test(PREFIX bit
                 SOURCES bit.c
                 LIBRARIES bit
)
create_unit_test(PREFIX bitset_basic
                 SOURCES bitset_basic.c
                 LIBRARIES bitset
)
create_unit_test(PREFIX bitset_iterator
                 SOURCES bitset_iterator.c
                 LIBRARIES bitset
)
create_unit_test(PREFIX bitset_index
                 SOURCES bitset_index.c
                 LIBRARIES bitset
)
create_unit_test(PREFIX base64
                 SOURCES base64.c
                 LIBRARIES misc unit
)
create_unit_test(PREFIX uuid
                 SOURCES uuid.c core_test_utils.c
                 LIBRARIES core unit
)
create_unit_test(PREFIX random
                 SOURCES random.c core_test_utils.c
                 LIBRARIES core unit
)
create_unit_test(PREFIX xmalloc
                 SOURCES xmalloc.c core_test_utils.c
                 LIBRARIES unit
)
create_unit_test(PREFIX datetime
                 SOURCES datetime.c
                 LIBRARIES tzcode core cdt unit
)
create_unit_test(PREFIX error
                 SOURCES error.c core_test_utils.c
                 LIBRARIES unit core box_error
)
create_unit_test(PREFIX interval
                 SOURCES interval.c core_test_utils.c
                 LIBRARIES core unit
)
create_unit_test(PREFIX bps_tree
                 SOURCES bps_tree.cc
                 LIBRARIES small misc
)
create_unit_test(PREFIX bps_tree_iterator
                 SOURCES bps_tree_iterator.cc
                 LIBRARIES small misc
)
create_unit_test(PREFIX bps_tree_view
                 SOURCES bps_tree_view.c
                 LIBRARIES small unit
)
create_unit_test(PREFIX rtree
                 SOURCES rtree.cc
                 LIBRARIES salad small
)
create_unit_test(PREFIX rtree_iterator
                 SOURCES rtree_iterator.cc
                 LIBRARIES salad small
)
create_unit_test(PREFIX rtree_multidim
                 SOURCES rtree_multidim.cc
                 LIBRARIES salad small
)
create_unit_test(PREFIX light
                 SOURCES light.cc
                 LIBRARIES small
)
create_unit_test(PREFIX light_view
                 SOURCES light_view.c
                 LIBRARIES small unit
)
create_unit_test(PREFIX bloom
                 SOURCES bloom.cc
                 LIBRARIES salad
)
create_unit_test(PREFIX vclock
                 SOURCES vclock.cc
                 LIBRARIES vclock unit
)
create_unit_test(PREFIX xrow
                 SOURCES xrow.cc core_test_utils.c
                 LIBRARIES xrow unit
)
create_unit_test(PREFIX xlog
                 SOURCES xlog.c core_test_utils.c
                 LIBRARIES xlog xrow unit
)
create_unit_test(PREFIX decimal
                 SOURCES decimal.c
                 LIBRARIES core unit
)
create_unit_test(PREFIX mp_error
                 SOURCES mp_error.cc core_test_utils.c
                 LIBRARIES box_error core unit
)
create_unit_test(PREFIX fiber
                 SOURCES fiber.cc core_test_utils.c
                 LIBRARIES core unit
)

if(NOT ${CMAKE_BUILD_TYPE} EQUAL "Release")
    create_unit_test(PREFIX fiber_stack
                     SOURCES fiber_stack.c core_test_utils.c
                     LIBRARIES core unit
    )
endif()

create_unit_test(PREFIX func_cache
                 SOURCES func_cache.c box_test_utils.c
                 LIBRARIES box unit
)
create_unit_test(PREFIX prbuf
                 SOURCES prbuf.c core_test_utils.c
                 LIBRARIES unit core
)
create_unit_test(PREFIX clock_lowres
                 SOURCES clock_lowres.c core_test_utils.c
                 LIBRARIES unit core
)
create_unit_test(PREFIX trigger
                 SOURCES trigger.c core_test_utils.c
                 LIBRARIES unit core
)

if (NOT ENABLE_GCOV)
    # This test is known to be broken with GCOV
    create_unit_test(PREFIX guard
                     SOURCES guard.cc core_test_utils.c
                     LIBRARIES core unit
    )
endif ()

create_unit_test(PREFIX fiber_stress
                 SOURCES fiber_stress.cc core_test_utils.c
                 LIBRARIES core
)
create_unit_test(PREFIX fiber_cond
                 SOURCES fiber_cond.c unit.c core_test_utils.c
                 LIBRARIES core
)
create_unit_test(PREFIX fiber_channel
                 SOURCES fiber_channel.cc unit.c core_test_utils.c
                 LIBRARIES core
)
create_unit_test(PREFIX fiber_channel_stress
                 SOURCES fiber_channel_stress.cc core_test_utils.c
                 LIBRARIES core
)

create_unit_test(PREFIX cbus_stress
                 SOURCES cbus_stress.c core_test_utils.c
                 LIBRARIES core stat
)

create_unit_test(PREFIX cbus
                 SOURCES cbus.c core_test_utils.c
                 LIBRARIES core unit stat
)

create_unit_test(PREFIX cbus_call
                 SOURCES cbus_call.c core_test_utils.c
                 LIBRARIES core unit stat
)

include(CheckSymbolExists)
check_symbol_exists(__GLIBC__ features.h GLIBC_USED)
if (GLIBC_USED)
    create_unit_test(PREFIX cbus_hang
                     SOURCES cbus_hang.c core_test_utils.c
                     LIBRARIES core unit stat
    )
endif ()

create_unit_test(PREFIX coio
                 SOURCES coio.cc core_test_utils.c
                 LIBRARIES core eio bit uri unit
)

if (ENABLE_BUNDLED_MSGPUCK)
    set(MSGPUCK_DIR ${PROJECT_SOURCE_DIR}/src/lib/msgpuck/)
    set_source_files_properties(
        ${MSGPUCK_DIR}/test/msgpuck.c
        ${MSGPUCK_DIR}/test/test.c
        PROPERTIES COMPILE_FLAGS "-I${MSGPUCK_DIR}/test")

    create_unit_test(PREFIX msgpack
                     SOURCES ${MSGPUCK_DIR}/test/msgpuck.c
                             ${MSGPUCK_DIR}/test/test.c
                     LIBRARIES ${MSGPUCK_LIBRARIES}
    )
endif ()

create_unit_test(PREFIX mp_print_unknown_ext
                 SOURCES mp_print_unknown_ext.c box_test_utils.c
                 LIBRARIES core unit box core
)

create_unit_test(PREFIX guava
                 SOURCES guava.c
                 LIBRARIES salad small
)

create_unit_test(PREFIX crc32
                 SOURCES crc32.c
                 LIBRARIES unit crc32
)

create_unit_test(PREFIX find_path
                 SOURCES find_path.c core_test_utils.c
                         ${PROJECT_SOURCE_DIR}/src/find_path.c
                 LIBRARIES core
)

create_unit_test(PREFIX reflection_c
                 SOURCES reflection_c.c unit.c
                         ${PROJECT_SOURCE_DIR}/src/lib/core/reflection.c
                 LIBRARIES
)

create_unit_test(PREFIX reflection_cxx
                 SOURCES reflection_cxx.cc unit.c
                         ${PROJECT_SOURCE_DIR}/src/lib/core/reflection.c
                 LIBRARIES
)

create_unit_test(PREFIX csv
                 SOURCES csv.c
                 LIBRARIES csv
)

create_unit_test(PREFIX json
                 SOURCES json.c
                 LIBRARIES json unit ${ICU_LIBRARIES}
)

create_unit_test(PREFIX http_parser
                 SOURCES http_parser.c
                 LIBRARIES unit http_parser
)

create_unit_test(PREFIX rmean
                 SOURCES rmean.cc core_test_utils.c
                 LIBRARIES stat unit
)

create_unit_test(PREFIX histogram
                 SOURCES histogram.c core_test_utils.c
                 LIBRARIES stat unit
)

create_unit_test(PREFIX ratelimit
                 SOURCES ratelimit.c
                 LIBRARIES unit
)

create_unit_test(PREFIX luaT_tuple_new
                 SOURCES luaT_tuple_new.c box_test_utils.c
                 LIBRARIES unit box server core misc
                           ${CURL_LIBRARIES}
                           ${LIBYAML_LIBRARIES}
                           ${READLINE_LIBRARIES}
                           ${ICU_LIBRARIES}
                           ${LUAJIT_LIBRARIES}
)

create_unit_test(PREFIX luaL_iterator
                 SOURCES luaL_iterator.c
                 LIBRARIES unit server coll core misc
                           ${CURL_LIBRARIES}
                           ${LIBYAML_LIBRARIES}
                           ${READLINE_LIBRARIES}
                           ${ICU_LIBRARIES}
                           ${LUAJIT_LIBRARIES}
                           ${LIB_DL}
)

create_unit_test(PREFIX say
                 SOURCES say.c core_test_utils.c
                 LIBRARIES core unit
)

set(ITERATOR_TEST_SOURCES
    vy_iterators_helper.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_stmt.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_upsert.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_history.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_mem.c
    ${PROJECT_SOURCE_DIR}/src/box/vy_cache.c)
set(ITERATOR_TEST_LIBS core tuple xrow unit)

create_unit_test(PREFIX vy_mem
                 SOURCES vy_mem.c ${ITERATOR_TEST_SOURCES} core_test_utils.c
                 LIBRARIES ${ITERATOR_TEST_LIBS} ${LIB_DL}
)

create_unit_test(PREFIX vy_point_lookup
                 SOURCES vy_point_lookup.c
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
                 LIBRARIES core tuple xrow xlog unit ${LIB_DL}
)

create_unit_test(PREFIX column_mask
                 SOURCES column_mask.c core_test_utils.c
                 LIBRARIES tuple unit
)

create_unit_test(PREFIX vy_write_iterator
                 SOURCES vy_write_iterator.c
                         ${PROJECT_SOURCE_DIR}/src/box/vy_run.c
                         ${PROJECT_SOURCE_DIR}/src/box/vy_upsert.c
                         ${PROJECT_SOURCE_DIR}/src/box/vy_write_iterator.c
                         ${ITERATOR_TEST_SOURCES}
                         core_test_utils.c
                 LIBRARIES xlog ${ITERATOR_TEST_LIBS} ${LIB_DL}
)

create_unit_test(PREFIX vy_cache
                 SOURCES vy_cache.c ${ITERATOR_TEST_SOURCES} core_test_utils.c
                 LIBRARIES ${ITERATOR_TEST_LIBS} ${LIB_DL}
)

create_unit_test(PREFIX coll
                 SOURCES coll.cpp core_test_utils.c
                 LIBRARIES coll unit misc ${LIB_DL}
)

create_unit_test(PREFIX tuple_bigref
                 SOURCES tuple_bigref.c core_test_utils.c
                 LIBRARIES tuple unit
)

create_unit_test(PREFIX tuple_uint32_overflow
                 SOURCES tuple_uint32_overflow.c core_test_utils.c
                 LIBRARIES tuple unit
)

create_unit_test(PREFIX checkpoint_schedule
                 SOURCES checkpoint_schedule.c
                         ${PROJECT_SOURCE_DIR}/src/box/checkpoint_schedule.c
                 LIBRARIES m unit
)

create_unit_test(PREFIX sio
                 SOURCES sio.c core_test_utils.c
                 LIBRARIES unit core
)

create_unit_test(PREFIX crypto
                 SOURCES crypto.c core_test_utils.c
                 LIBRARIES crypto unit
)

create_unit_test(PREFIX swim
                 SOURCES swim.c swim_test_transport.c swim_test_ev.c
                         swim_test_utils.c ${PROJECT_SOURCE_DIR}/src/version.c
                         core_test_utils.c
                 LIBRARIES unit fakesys swim
)

create_unit_test(PREFIX swim_proto
                 SOURCES swim_proto.c swim_test_transport.c swim_test_ev.c
                         swim_test_utils.c ${PROJECT_SOURCE_DIR}/src/version.c
                         core_test_utils.c
                 LIBRARIES unit fakesys swim
)

if(NOT ${CMAKE_BUILD_TYPE} EQUAL "Release")
    create_unit_test(PREFIX swim_errinj
                     SOURCES swim_errinj.c swim_test_transport.c swim_test_ev.c
                             swim_test_utils.c ${PROJECT_SOURCE_DIR}/src/version.c
                             core_test_utils.c
                     LIBRARIES unit fakesys swim
    )
endif()

create_unit_test(PREFIX merger
                 SOURCES merger.test.c box_test_utils.c
                 LIBRARIES unit core box
)

create_unit_test(PREFIX snap_quorum_delay
                 SOURCES snap_quorum_delay.cc box_test_utils.c
                 LIBRARIES box core unit
)

create_unit_test(PREFIX raft
                 SOURCES raft.c raft_test_utils.c core_test_utils.c
                 LIBRARIES vclock unit fakesys raft_algo
)

#
# Client for popen.test
add_executable(popen-child popen-child.c)

create_unit_test(PREFIX popen
                 SOURCES popen.c core_test_utils.c
                 LIBRARIES misc unit core
)
add_dependencies(popen.test popen-child)

create_unit_test(PREFIX serializer
                 SOURCES serializer.c box_test_utils.c
                 LIBRARIES unit box ${LUAJIT_LIBRARIES}
)

create_unit_test(PREFIX watcher
                 SOURCES watcher.c box_test_utils.c
                 LIBRARIES unit box
)

create_unit_test(PREFIX grp_alloc
                 SOURCES grp_alloc.c box_test_utils.c
                 LIBRARIES unit
)

create_unit_test(PREFIX latch
                 SOURCES latch.c core_test_utils.c
                 LIBRARIES core unit stat
)

create_unit_test(PREFIX memtx_allocator
                 SOURCES memtx_allocator.cc box_test_utils.c
                 LIBRARIES unit core box
)

create_unit_test(PREFIX tt_sigaction
                 SOURCES tt_sigaction.c core_test_utils.c
                 LIBRARIES core unit pthread
)

create_unit_test(PREFIX string
                 SOURCES string.c core_test_utils.c
                 LIBRARIES core unit
)

create_unit_test(PREFIX qsort_arg
                 SOURCES qsort_arg.cc core_test_utils.c
                 LIBRARIES misc unit
)

create_unit_test(PREFIX tt_sort
                 SOURCES tt_sort.cc core_test_utils.c
                 LIBRARIES unit core
)

create_unit_test(PREFIX iterator_position
                 SOURCES iterator_position.c box_test_utils.c
                 LIBRARIES unit box core
)

create_unit_test(PREFIX key_def
                 SOURCES key_def.c box_test_utils.c
                 LIBRARIES unit box core
)

create_unit_test(PREFIX tuple_builder
                 SOURCES tuple_builder.c box_test_utils.c
                 LIBRARIES unit box core
)

create_unit_test(PREFIX tuple_format
                 SOURCES tuple_format.c box_test_utils.c
                 LIBRARIES unit box core
)

create_unit_test(PREFIX getenv_safe
                 SOURCES getenv_safe.c core_test_utils.c
                 LIBRARIES unit core
)

create_unit_test(PREFIX lua_utils
                 SOURCES lua_utils.c
                 LIBRARIES unit core server
                           ${LUAJIT_LIBRARIES}
                           ${CURL_LIBRARIES}
                           ${LIBYAML_LIBRARIES}
                           ${READLINE_LIBRARIES}
)

create_unit_test(PREFIX lua_msgpack
                 SOURCES lua_msgpack.c
                 LIBRARIES unit core server
                           ${LUAJIT_LIBRARIES}
                           ${CURL_LIBRARIES}
                           ${LIBYAML_LIBRARIES}
                           ${READLINE_LIBRARIES}
)

create_unit_test(PREFIX tweaks
                 SOURCES tweaks.c core_test_utils.c
                 LIBRARIES core unit
)

create_unit_test(PREFIX lua_tweaks
                 SOURCES lua_tweaks.c
                 LIBRARIES unit core server
                           ${LUAJIT_LIBRARIES}
                           ${CURL_LIBRARIES}
                           ${LIBYAML_LIBRARIES}
                           ${READLINE_LIBRARIES}
)

create_unit_test(PREFIX node_name
                 SOURCES node_name.c core_test_utils.c
                 LIBRARIES core unit node_name
)

create_unit_test(PREFIX lua_func_adapter
                 SOURCES lua_func_adapter.c box_test_utils.c
                 LIBRARIES unit box server core misc
                           ${CURL_LIBRARIES}
                           ${LIBYAML_LIBRARIES}
                           ${READLINE_LIBRARIES}
                           ${ICU_LIBRARIES}
                           ${LUAJIT_LIBRARIES}
)

create_unit_test(PREFIX event
                 SOURCES event.c core_test_utils.c
                 LIBRARIES core unit
)
