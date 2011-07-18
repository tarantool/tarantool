/*
 * Copyright (C) 2010, 2011 Mail.RU
 * Copyright (C) 2010, 2011 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "tarantool.h"
#include "box.h"
#include "fiber.h"
#include "cfg/warning.h"
#include "cfg/tarantool_box_cfg.h"
#include "say.h"
#include "stat.h"
#include "salloc.h"

#define STAT(_)					\
        _(MEMC_GET, 1)				\
        _(MEMC_GET_MISS, 2)			\
	_(MEMC_GET_HIT, 3)			\
	_(MEMC_EXPIRED_KEYS, 4)

ENUM(memcached_stat, STAT);
STRS(memcached_stat, STAT);

static int stat_base;
static struct fiber *memcached_expire = NULL;

static struct index *memcached_index;

/* memcached tuple format:
   <key, meta, data> */

struct meta {
	u32 exptime;
	u32 flags;
	u64 cas;
} __packed__;

static u64
natoq(const u8 *start, const u8 *end)
{
	u64 num = 0;
	while (start < end)
		num = num * 10 + (*start++ - '0');
	return num;
}

static void
store(void *key, u32 exptime, u32 flags, u32 bytes, u8 *data)
{
	u32 box_flags = BOX_QUIET, cardinality = 4;
	static u64 cas = 42;
	struct meta m;

	struct tbuf *req = tbuf_alloc(fiber->pool);

	tbuf_append(req, &cfg.memcached_namespace, sizeof(u32));
	tbuf_append(req, &box_flags, sizeof(box_flags));
	tbuf_append(req, &cardinality, sizeof(cardinality));

	tbuf_append_field(req, key);

	m.exptime = exptime;
	m.flags = flags;
	m.cas = cas++;
	write_varint32(req, sizeof(m));
	tbuf_append(req, &m, sizeof(m));

	char b[43];
	sprintf(b, " %"PRIu32" %"PRIu32"\r\n", flags, bytes);
	write_varint32(req, strlen(b));
	tbuf_append(req, b, strlen(b));

	write_varint32(req, bytes);
	tbuf_append(req, data, bytes);

	int key_len = load_varint32(&key);
	say_debug("memcached/store key:(%i)'%.*s' exptime:%"PRIu32" flags:%"PRIu32" cas:%"PRIu64,
		  key_len, key_len, (u8 *)key, exptime, flags, cas);
	/*
	 * Use a box dispatch wrapper which handles correctly
	 * read-only/read-write modes.
	 */
	rw_callback(INSERT, req);
}

static void
delete(void *key)
{
	u32 key_len = 1;
	u32 box_flags = BOX_QUIET;
	struct tbuf *req = tbuf_alloc(fiber->pool);

	tbuf_append(req, &cfg.memcached_namespace, sizeof(u32));
	tbuf_append(req, &box_flags, sizeof(box_flags));
	tbuf_append(req, &key_len, sizeof(key_len));
	tbuf_append_field(req, key);

	rw_callback(DELETE, req);
}

static struct box_tuple *
find(void *key)
{
	return memcached_index->find(memcached_index, key);
}

static struct meta *
meta(struct box_tuple *tuple)
{
	void *field = tuple_field(tuple, 1);
	return field + 1;
}

static bool
expired(struct box_tuple *tuple)
{
	struct meta *m = meta(tuple);
	return m->exptime == 0 ? 0 : m->exptime < ev_now();
}

static bool
is_numeric(void *field, u32 value_len)
{
	for (int i = 0; i < value_len; i++)
		if (*((u8 *)field + i) < '0' || '9' < *((u8 *)field + i))
			return false;
	return true;
}

static struct stats {
	u64 total_items;
	u32 curr_connections;
	u32 total_connections;
	u64 cmd_get;
	u64 cmd_set;
	u64 get_hits;
	u64 get_misses;
	u64 evictions;
	u64 bytes_read;
	u64 bytes_written;
} stats;

static void
print_stats()
{
	u64 bytes_used, items;
	struct tbuf *out = tbuf_alloc(fiber->pool);
	slab_stat2(&bytes_used, &items);

	tbuf_printf(out, "STAT pid %"PRIu32"\r\n", (u32)getpid());
	tbuf_printf(out, "STAT uptime %"PRIu32"\r\n", (u32)tarantool_uptime());
	tbuf_printf(out, "STAT time %"PRIu32"\r\n", (u32)ev_now());
	tbuf_printf(out, "STAT version 1.2.5 (tarantool/box)\r\n");
	tbuf_printf(out, "STAT pointer_size %"PRI_SZ"\r\n", sizeof(void *)*8);
	tbuf_printf(out, "STAT curr_items %"PRIu64"\r\n", items);
	tbuf_printf(out, "STAT total_items %"PRIu64"\r\n", stats.total_items);
	tbuf_printf(out, "STAT bytes %"PRIu64"\r\n", bytes_used);
	tbuf_printf(out, "STAT curr_connections %"PRIu32"\r\n", stats.curr_connections);
	tbuf_printf(out, "STAT total_connections %"PRIu32"\r\n", stats.total_connections);
	tbuf_printf(out, "STAT connection_structures %"PRIu32"\r\n", stats.curr_connections); /* lie a bit */
	tbuf_printf(out, "STAT cmd_get %"PRIu64"\r\n", stats.cmd_get);
	tbuf_printf(out, "STAT cmd_set %"PRIu64"\r\n", stats.cmd_set);
	tbuf_printf(out, "STAT get_hits %"PRIu64"\r\n", stats.get_hits);
	tbuf_printf(out, "STAT get_misses %"PRIu64"\r\n", stats.get_misses);
	tbuf_printf(out, "STAT evictions %"PRIu64"\r\n", stats.evictions);
	tbuf_printf(out, "STAT bytes_read %"PRIu64"\r\n", stats.bytes_read);
	tbuf_printf(out, "STAT bytes_written %"PRIu64"\r\n", stats.bytes_written);
	tbuf_printf(out, "STAT limit_maxbytes %"PRIu64"\r\n", (u64)(cfg.slab_alloc_arena * (1 << 30)));
	tbuf_printf(out, "STAT threads 1\r\n");
	tbuf_printf(out, "END\r\n");
	add_iov(out->data, out->len);
}

static void
flush_all(void *data)
{
	uintptr_t delay = (uintptr_t)data;
	fiber_sleep(delay - ev_now());
	khash_t(lstr_ptr_map) *map = memcached_index->idx.str_hash;
	for (khiter_t i = kh_begin(map); i != kh_end(map); i++) {
		if (kh_exist(map, i)) {
			struct box_tuple *tuple = kh_value(map, i);
			meta(tuple)->exptime = 1;
		}
	}
}

#define STORE									\
do {										\
	stats.cmd_set++;							\
	if (bytes > (1<<20)) {							\
		add_iov("SERVER_ERROR object too large for cache\r\n", 41);	\
	} else {								\
		@try {								\
			store(key, exptime, flags, bytes, data);		\
			stats.total_items++;					\
			add_iov("STORED\r\n", 8);				\
		}								\
		@catch (ClientError *e) {					\
			add_iov("SERVER_ERROR ", 13);				\
			add_iov(e->errmsg, strlen(e->errmsg));			\
			add_iov("\r\n", 2);					\
		}								\
	}									\
} while (0)

#include "memcached-grammar.m"

void
memcached_handler(void *_data __attribute__((unused)))
{
	stats.total_connections++;
	stats.curr_connections++;
	int r, p;
	int batch_count;

	for (;;) {
		batch_count = 0;
		if ((r = fiber_bread(fiber->rbuf, 1)) <= 0) {
			say_debug("read returned %i, closing connection", r);
			goto exit;
		}

	dispatch:
		p = memcached_dispatch();
		if (p < 0) {
			say_debug("negative dispatch, closing connection");
			goto exit;
		}

		if (p == 0 && batch_count == 0) /* we havn't successfully parsed any requests */
			continue;

		if (p == 1) {
			batch_count++;
			/* some unparsed commands remain and batch count less than 20 */
			if (fiber->rbuf->len > 0 && batch_count < 20)
				goto dispatch;
		}

		r = fiber_flush_output();
		if (r < 0) {
			say_debug("flush_output failed, closing connection");
			goto exit;
		}

		stats.bytes_written += r;
		fiber_gc();

		if (p == 1 && fiber->rbuf->len > 0) {
			batch_count = 0;
			goto dispatch;
		}
	}
exit:
        fiber_flush_output();
	fiber_sleep(0.01);
	say_debug("exit");
	stats.curr_connections--; /* FIXME: nonlocal exit via exception will leak this counter */
}


int
memcached_check_config(struct tarantool_cfg *conf)
{
	if (conf->memcached_port == 0) {
		return 0;
	}

	if (conf->memcached_port <= 0 || conf->memcached_port >= USHRT_MAX) {
		/* invalid namespace number */
		out_warning(0, "invalid memcached port value: %i",
			    conf->memcached_port);
		return -1;
	}

	/* check memcached namespace number: it shoud be in segment [0, max_namespace] */
	if ((conf->memcached_namespace < 0) ||
	    (conf->memcached_namespace > BOX_NAMESPACE_MAX)) {
		/* invalid namespace number */
		out_warning(0, "invalid memcached namespace number: %i",
			    conf->memcached_namespace);
		return -1;
	}

	if (conf->memcached_expire_per_loop <= 0) {
		/* invalid expire per loop value */
		out_warning(0, "invalid expire per loop value: %i",
			    conf->memcached_expire_per_loop);
		return -1;
	}

	if (conf->memcached_expire_full_sweep <= 0) {
		/* invalid expire full sweep value */
		out_warning(0, "invalid expire full sweep value: %i",
			    conf->memcached_expire_full_sweep);
		return -1;
	}

	return 0;
}

void
memcached_init(void)
{
	if (cfg.memcached_port == 0) {
		return;
	}

	stat_base = stat_register(memcached_stat_strs, memcached_stat_MAX);

	memcached_index = &namespace[cfg.memcached_namespace].index[0];
}

void
memcached_namespace_init()
{
	struct namespace *memc_ns;
	struct index *memc_index;

	/* configure memcached namespace */
	memc_ns = &namespace[cfg.memcached_namespace];
	memc_ns->enabled = true;
	memc_ns->cardinality = 4;
	memc_ns->n = cfg.memcached_namespace;

	/* configure memcached index */
	memc_index = &memc_ns->index[0];

	/* configure memcached index's key */
	memc_index->key_cardinality = 1;

	memc_index->key_field = salloc(sizeof(memc_index->key_field[0]));
	memc_index->field_cmp_order = salloc(sizeof(u32));
	memc_index->search_pattern = palloc(eter_pool, SIZEOF_TREE_INDEX_MEMBER(memc_index));

	if (memc_index->key_field == NULL || memc_index->field_cmp_order == NULL ||
	    memc_index->search_pattern == NULL)
		panic("out of memory when configuring memcached_namespace");

	memc_index->key_field[0].fieldno = 0;
	memc_index->key_field[0].type = STRING;

	/* configure memcached index compare order */
	memc_index->field_cmp_order_cnt = 1;
	memc_index->field_cmp_order[0] = 0;

	memc_index->unique = true;
	memc_index->type = HASH;
	memc_index->enabled = true;
	index_init(memc_index, memc_ns, 0);
}

void
memcached_namespace_free()
{
	struct namespace *memc_ns;
	struct index *memc_index;

	memc_ns = &namespace[cfg.memcached_namespace];
	memc_index = &memc_ns->index[0];

	index_free(memc_index);

	sfree(memc_index->key_field);
	sfree(memc_index->field_cmp_order);
}

void
memcached_expire_loop(void *data __attribute__((unused)))
{
	static khiter_t i;
	khash_t(lstr_ptr_map) *map = memcached_index->idx.str_hash;

	say_info("memcached expire fiber started");
	for (;;) {
		if (i > kh_end(map))
			i = kh_begin(map);

		struct tbuf *keys_to_delete = tbuf_alloc(fiber->pool);
		int expired_keys = 0;

		for (int j = 0; j < cfg.memcached_expire_per_loop; j++, i++) {
			if (i == kh_end(map)) {
				i = kh_begin(map);
				break;
			}

			if (!kh_exist(map, i))
				continue;

			struct box_tuple *tuple = kh_value(map, i);

			if (!expired(tuple))
				continue;

			say_debug("expire tuple %p", tuple);
			tbuf_append_field(keys_to_delete, tuple->data);
		}

		while (keys_to_delete->len > 0) {
			@try {
				delete(read_field(keys_to_delete));
				expired_keys++;
			}
			@catch (ClientError *e) {
				/* expire is off when replication is on */
				assert(e->errcode != ER_NONMASTER);
				/* The error is already logged. */
			}
		}
		stat_collect(stat_base, MEMC_EXPIRED_KEYS, expired_keys);

		fiber_gc();

		double delay = (double)cfg.memcached_expire_per_loop * cfg.memcached_expire_full_sweep / (map->size + 1);
		if (delay > 1)
			delay = 1;
		fiber_setcancelstate(true);
		fiber_sleep(delay);
		fiber_setcancelstate(false);
	}
}

void memcached_start_expire()
{
	if (cfg.memcached_port == 0 || cfg.memcached_expire == 0)
		return;

	assert(memcached_expire == NULL);
	memcached_expire = fiber_create("memecached_expire", -1,
					-1, memcached_expire_loop, NULL);
	if (memcached_expire == NULL)
		say_error("can't start the expire fiber");
	fiber_call(memcached_expire);
}

void memcached_stop_expire()
{
	if (cfg.memcached_port == 0 || cfg.memcached_expire == 0)
		return;
	assert(memcached_expire != NULL);
	fiber_cancel(memcached_expire);
	memcached_expire = NULL;
}
