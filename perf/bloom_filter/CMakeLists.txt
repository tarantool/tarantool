set(CMAKE_CXX_STANDARD 11)

add_library(bench SHARED bloom_filter.cc)
# for xmalloc
target_link_libraries(bench unit)
# -fPIC is required since we are going to load this lib during Tarantool runtime.
set_target_properties(bench PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Don't mangle module name as we are going to load it manually in Lua script
set_target_properties(bench PROPERTIES PREFIX "")
