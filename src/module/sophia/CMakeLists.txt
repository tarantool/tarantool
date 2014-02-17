include_directories(${SOPHIA_INCLUDE_DIR})
set (sophia_lib "${PROJECT_BINARY_DIR}/third_party/sophia/db/libsophia.a")

string(REPLACE "-static" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "-static-libgcc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

add_library(sophia SHARED sophia.cc)
set_target_properties(sophia PROPERTIES PREFIX "")
target_link_libraries(sophia ${sophia_lib} -rdynamic)

install(TARGETS sophia LIBRARY DESTINATION ${MODULE_DIR}/box)
