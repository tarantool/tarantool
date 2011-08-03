set (tnt_sources 
	tnt_opt.c
	tnt_buf.c
	tnt_mem.c
	tnt_leb128.c
	tnt_tuple.c
	tnt.c
	tnt_io.c
	tnt_recv.c
	tnt_ping.c
	tnt_update.c
	tnt_insert.c
	tnt_delete.c
	tnt_select.c
	tnt_memcache_val.c
	tnt_memcache.c)

add_library(tnt STATIC ${tnt_sources})

add_library(tnt_shared SHARED ${tnt_sources})
set_target_properties(tnt_shared PROPERTIES OUTPUT_NAME tnt)	

install(FILES
	include/tnt.h
	include/tnt_queue.h
	include/tnt_error.h
	include/tnt_mem.h
	include/tnt_opt.h
	include/tnt_buf.h
	include/tnt_main.h
	include/tnt_io.h
	include/tnt_proto.h
	include/tnt_tuple.h
	include/tnt_insert.h
	include/tnt_update.h
	include/tnt_delete.h
	include/tnt_select.h
	include/tnt_ping.h
	include/tnt_recv.h
	include/tnt_memcache_val.h
	include/tnt_memcache.h
	DESTINATION include/libtnt)

install_targets(/lib tnt)
install_targets(/lib tnt_shared)
