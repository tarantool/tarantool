include_directories(${SOPHIA_INCLUDE_DIR})
add_library(sophia SHARED sophia.cc)

target_link_libraries(sophia
	${PROJECT_BINARY_DIR}/third_party/sophia/db/libsophia.a -rdynamic)

set_target_properties(sophia PROPERTIES PREFIX "")

install(TARGETS sophia LIBRARY DESTINATION ${MODULE_DIR}/box)
