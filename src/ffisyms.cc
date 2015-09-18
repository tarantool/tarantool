#include <bit/bit.h>
#include <lib/msgpuck/msgpuck.h>
#include "scramble.h"
#include <box/box.h>
#include <box/tuple.h>
#include <box/index.h>
#include <box/func.h>
#include <box/lua/tuple.h>
#include <box/lua/call.h>
#include <box/sophia_engine.h>
#include <box/request.h>
#include <box/port.h>
#include <box/xrow.h>
#include <lua/init.h>
#include "main.h"
#include "lua/bsdsocket.h"
#include "lua/digest.h"
#include "fiber.h"
#include "base64.h"
#include "random.h"
#include "iobuf.h"
#include <lib/salad/guava.h>
#include "latch.h"
#include <lib/csv/csv.h>
#include <lua/clock.h>

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
	(void *) box_select,
	(void *) box_insert,
	(void *) box_replace,
	(void *) box_delete,
	(void *) box_update,
	(void *) box_upsert,
	(void *) box_tuple_field_count,
	(void *) box_tuple_field,
	(void *) box_tuple_rewind,
	(void *) box_tuple_seek,
	(void *) box_tuple_next,
	(void *) box_tuple_ref,
	(void *) box_tuple_unref,
	(void *) box_tuple_to_buf,
	(void *) box_index_len,
	(void *) box_index_bsize,
	(void *) box_index_random,
	(void *) box_index_get,
	(void *) box_index_min,
	(void *) box_index_max,
	(void *) box_index_count,
	(void *) box_index_iterator,
	(void *) box_iterator_next,
	(void *) boxffi_tuple_update,
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
	(void *) clock_realtime,
	(void *) clock_monotonic,
	(void *) clock_process,
	(void *) clock_thread,
	(void *) clock_realtime64,
	(void *) clock_monotonic64,
	(void *) clock_process64,
	(void *) clock_thread64,
	(void *) tarantool_lua_slab_cache,
	(void *) ibuf_create,
	(void *) ibuf_destroy,
	(void *) ibuf_reserve_nothrow_slow,
	(void *) port_buf_create,
	(void *) port_buf_destroy,
	(void *) port_buf_transfer,
	(void *) box_return_tuple,
	(void *) box_error_type,
	(void *) box_error_code,
	(void *) box_error_message,
	(void *) box_error_clear,
	(void *) box_error_last,
	(void *) box_latch_new,
	(void *) box_latch_delete,
	(void *) box_latch_lock,
	(void *) box_latch_trylock,
	(void *) box_latch_unlock,
	(void *) csv_create,
	(void *) csv_destroy,
	(void *) csv_setopt,
	(void *) csv_iterator_create,
	(void *) csv_next,
	(void *) csv_feed,
	(void *) greeting_decode
};
