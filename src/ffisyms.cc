
#include <bit/bit.h>
#include <lib/msgpuck/msgpuck.h>
#include "scramble.h"
#include <box/box.h>
#include <box/tuple.h>
#include <box/lua/index.h>
#include <box/lua/tuple.h>
#include <box/lua/call.h>
#include <box/sophia_engine.h>
#include <lua/init.h>
#include <tarantool.h>
#include "lua/bsdsocket.h"
#include "lua/digest.h"
#include "fiber.h"
#include "base64.h"
#include "random.h"
#include <lib/salad/guava.h>

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
	(void *) tuple_field_count,
	(void *) tuple_field,
	(void *) tuple_rewind,
	(void *) tuple_seek,
	(void *) tuple_next,
	(void *) tuple_unref,
	(void *) boxffi_index_len,
	(void *) boxffi_index_memsize,
	(void *) boxffi_index_random,
	(void *) boxffi_index_get,
	(void *) boxffi_index_iterator,
	(void *) boxffi_tuple_update,
	(void *) boxffi_iterator_next,
	(void *) port_ffi_create,
	(void *) port_ffi_destroy,
	(void *) boxffi_select,
	(void *) password_prepare,
	(void *) tarantool_error_message,
	(void *) load_cfg,
	(void *) box_set_listen,
	(void *) box_set_replication_source,
	(void *) box_set_wal_mode,
	(void *) box_set_log_level,
	(void *) box_set_io_collect_interval,
	(void *) box_set_snap_io_rate_limit,
	(void *) box_set_too_long_threshold,
	(void *) bsdsocket_local_resolve,
	(void *) bsdsocket_nonblock,
	(void *) base64_decode,
	(void *) base64_encode,
	(void *) base64_bufsize,
	(void *) SHA1internal,
	(void *) guava,
	(void *) random_bytes,
	(void *) fiber_time,
	(void *) fiber_time64,
	(void *) sophia_schedule
};
