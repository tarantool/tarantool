/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <bit/bit.h>
#include <lib/msgpuck/msgpuck.h>
#include "scramble.h"
#include <box/box.h>
#include <box/tuple.h>
#include <box/index.h>
#include <box/func.h>
#include <box/sophia_engine.h>
#include <box/request.h>
#include <box/port.h>
#include <box/xrow.h>
#include <lua/init.h>
#include "main.h"
#include "lua/socket.h"
#include "lua/digest.h"
#include "fiber.h"
#include "base64.h"
#include "random.h"
#include "iobuf.h"
#include <lib/salad/guava.h>
#include "latch.h"
#include <lib/csv/csv.h>
#include <lua/clock.h>
#include "title.h"

/*
 * A special hack to cc/ld to keep symbols in an optimized binary.
 * Please add your symbols to this array if you plan to use it from
 * LuaJIT FFI.
 */
void *ffi_symbols[] = {
	(void *) bswap_u32,
	(void *) bswap_u64,
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
	(void *) box_tuple_update,
	(void *) box_tuple_upsert,
	(void *) password_prepare,
	(void *) load_cfg,
	(void *) box_set_listen,
	(void *) box_set_replication_source,
	(void *) box_set_log_level,
	(void *) box_set_io_collect_interval,
	(void *) box_set_snap_io_rate_limit,
	(void *) box_set_too_long_threshold,
	(void *) lbox_socket_local_resolve,
	(void *) lbox_socket_nonblock,
	(void *) base64_decode,
	(void *) base64_encode,
	(void *) base64_bufsize,
	(void *) SHA1internal,
	(void *) guava,
	(void *) random_bytes,
	(void *) fiber_time,
	(void *) fiber_time64,
	(void *) cord_slab_cache,
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
	(void *) ibuf_reserve_slow,
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
	(void *) greeting_decode,
	(void *) title_update,
	(void *) title_get,
	(void *) title_set_interpretor_name,
	(void *) title_get_interpretor_name,
	(void *) title_set_script_name,
	(void *) title_get_script_name,
	(void *) title_set_custom,
	(void *) title_get_custom,
	(void *) title_set_status,
	(void *) title_get_status,
	(void *) say_check_init_str,
};
