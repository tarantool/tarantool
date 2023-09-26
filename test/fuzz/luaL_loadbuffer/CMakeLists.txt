set(LUA_PREAMBLE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/preamble.lua")
if (NOT EXISTS "${LUA_PREAMBLE_PATH}")
   message(FATAL "File '${LUA_PREAMBLE_PATH}' is not found.")
endif ()

set(test_lua_sources)
lua_source(test_lua_sources preamble.lua preamble_lua)

add_custom_target(generate_test_lua_sources
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/test/fuzz/luaL_loadbuffer/
    DEPENDS ${test_lua_sources})
set_property(DIRECTORY PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${test_lua_sources})

add_executable(luaL_loadbuffer_fuzzer
               luaL_loadbuffer_fuzzer.cc
               serializer.cc
               ${CMAKE_CURRENT_BINARY_DIR}/preamble.lua.c)

add_library(lua_grammar-proto)

foreach(lib ${LPM_LIBRARIES})
    find_library(${lib} REQUIRED_FILES)
endforeach(lib)

protobuf_generate(LANGUAGE cpp
                  TARGET lua_grammar-proto
                  PROTOS lua_grammar.proto)

target_link_libraries(lua_grammar-proto
                      ${PROTOBUF_LIBRARIES})

target_include_directories(luaL_loadbuffer_fuzzer PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(luaL_loadbuffer_fuzzer
                      PUBLIC
                      lua_grammar-proto
                      ${LPM_LIBRARIES}
                      libluajit_static
                      fuzzer_config)

add_dependencies(luaL_loadbuffer_fuzzer
                 libluajit_static
                 ${LPM_LIBRARIES}
                 lua_grammar-proto)

set(FUZZ_TEST_TARGETS "${FUZZ_TEST_TARGETS};luaL_loadbuffer_fuzzer" PARENT_SCOPE)
