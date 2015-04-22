add_library(module_api SHARED module_api.c)
target_link_libraries(module_api -rdynamic)
set_target_properties(module_api PROPERTIES PREFIX "")
set_target_properties(module_api PROPERTIES INCLUDE_DIRECTORIES
    "${CMAKE_BINARY_DIR}/src/trivia;${CMAKE_SOURCE_DIR}/third_party/luajit/src/")
add_dependencies(module_api generate_module_api)
