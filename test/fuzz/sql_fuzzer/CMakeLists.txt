add_executable(sql_fuzzer
               sql_fuzzer.cc
               sql_query_proto_to_string.cc
               sql_query_proto_to_string.h)

add_library(sql-query-proto)

foreach(lib ${LPM_LIBRARIES})
    find_library(${lib} REQUIRED_FILES)
endforeach(lib)

protobuf_generate(LANGUAGE cpp
                  TARGET sql-query-proto
                  PROTOS sql_query.proto)

target_link_libraries(sql-query-proto
                      ${PROTOBUF_LIBRARIES})

include_directories(${PROJECT_BINARY_DIR}/src/box/sql)

target_include_directories(sql_fuzzer PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(sql_fuzzer PUBLIC box
                                        sql-query-proto
                                        ${LPM_LIBRARIES}
                                        fuzzer_config)

set(FUZZ_TEST_TARGETS "${FUZZ_TEST_TARGETS};sql_fuzzer" PARENT_SCOPE)
