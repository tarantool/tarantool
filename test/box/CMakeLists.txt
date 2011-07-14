add_executable(protocol protocol.c ${CMAKE_SOURCE_DIR}/core/errcode.c)
target_link_libraries (protocol client)

