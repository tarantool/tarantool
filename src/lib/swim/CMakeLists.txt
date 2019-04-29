set(lib_swim_sources swim.c swim_io.c swim_proto.c)
set(lib_swim_udp_sources swim_transport_udp.c)
set(lib_swim_ev_sources swim_ev.c)

set_source_files_compile_flags(${lib_swim_sources} ${lib_swim_udp_sources}
                               ${lib_swim_ev_sources})
add_library(swim STATIC ${lib_swim_sources})
target_link_libraries(swim core misc uuid crypto)
add_library(swim_udp STATIC ${lib_swim_udp_sources})
target_link_libraries(swim_udp core)
add_library(swim_ev STATIC ${lib_swim_ev_sources})
target_link_libraries(swim_ev core)
