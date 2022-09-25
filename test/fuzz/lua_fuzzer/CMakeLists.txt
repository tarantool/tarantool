add_executable(lua_fuzzer
               lua_fuzzer.cc
               serializer.cc)

add_library(lua_grammar-proto)

foreach(lib ${LPM_LIBRARIES})
    find_library(${lib} REQUIRED_FILES)
endforeach(lib)


protobuf_generate(LANGUAGE cpp
                  TARGET lua_grammar-proto
                  PROTOS lua_grammar.proto)

target_link_libraries(lua_grammar-proto
                      ${PROTOBUF_LIBRARIES})

target_include_directories(lua_fuzzer PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

set_target_properties(lua_fuzzer
    PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/.."
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/.."
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/.."
    OUTPUT_NAME "lua_loadbuffer_fuzzer"
)

target_link_libraries(lua_fuzzer
                      PUBLIC
                      lua_grammar-proto
                      ${LPM_LIBRARIES}
                      ${LUAJIT_LIBRARIES}
                      fuzzer_config)

add_dependencies(lua_fuzzer ${LPM_LIBRARIES} libluajit)
