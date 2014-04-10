#include <bit/bit.h>
#include <lib/msgpuck/msgpuck.h>
#include "scramble.h"
#include <box/box.h>
#include <box/tuple.h>
#include <box/lua/index.h>
#include <box/lua/call.h>
#include <lua/init.h>
#include <tarantool.h>

/*
 * A special hack to cc/ld to keep symbols in an optimized binary.
 * Please add your symbols to this array if you plan to use it from
 * LuaJIT FFI.
 */
void *ffi_symbols[] = {
	(void *) bswap_u32,
	(void *) bswap_u64,
	(void *) mp_bswap_float,
	(void *) mp_bswap_double,
	(void *) tuple_arity,
	(void *) tuple_field,
	(void *) tuple_rewind,
	(void *) tuple_seek,
	(void *) tuple_next,
	(void *) tuple_ref,
	(void *) boxffi_index_iterator,
	(void *) port_ffi_create,
	(void *) port_ffi_destroy,
	(void *) boxffi_select,
	(void *) password_prepare,
	(void *) tarantool_lua_interactive,
	(void *) load_cfg,
	(void *) box_set_wal_fsync_delay,
	(void *) box_set_replication_source,
	(void *) box_set_wal_mode,
	(void *) box_set_log_level,
	(void *) box_set_io_collect_interval,
	(void *) box_set_snap_io_rate_limit,
	(void *) box_set_too_long_threshold,
};
