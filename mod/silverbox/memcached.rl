/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
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

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

#include <errcode.h>
#include <salloc.h>
#include <palloc.h>
#include <fiber.h>
#include <util.h>
#include <pickle.h>

#include <tarantool.h>
#include <cfg/tarantool_silverbox_cfg.h>
#include <mod/silverbox/box.h>
#include <stat.h>


#define STAT(_)					\
        _(MEMC_GET, 1)				\
        _(MEMC_GET_MISS, 2)			\
	_(MEMC_GET_HIT, 3)			\
	_(MEMC_EXPIRED_KEYS, 4)

ENUM(memcached_stat, STAT);
STRS(memcached_stat, STAT);
int stat_base;

struct index *memcached_index;

/* memcached tuple format:
   <key, meta, data> */

struct meta {
	u32 exptime;
	u32 flags;
	u64 cas;
} __packed__;

%%{
	machine memcached;
	write data;
}%%


static u64
natoq(const u8 *start, const u8 *end)
{
	u64 num = 0;
	while (start < end)
		num = num * 10 + (*start++ - '0');
	return num;
}

static int
store(struct box_txn *txn, void *key, u32 exptime, u32 flags, u32 bytes, u8 *data)
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
	return box_dispach(txn, RW, INSERT, req); /* FIXME: handle RW/RO */
}

static int
delete(struct box_txn *txn, void *key)
{
	u32 key_len = 1;
	struct tbuf *req = tbuf_alloc(fiber->pool);

	tbuf_append(req, &cfg.memcached_namespace, sizeof(u32));
	tbuf_append(req, &key_len, sizeof(key_len));
	tbuf_append_field(req, key);

	return box_dispach(txn, RW, DELETE, req);
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
	tbuf_printf(out, "STAT version 1.2.5 (tarantool/silverbox)\r\n");
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


static int __noinline__
memcached_dispatch(struct box_txn *txn)
{
	int cs;
	u8 *p, *pe;
	u8 *fstart;
	struct tbuf *keys = tbuf_alloc(fiber->pool);
	void *key;
	bool append, show_cas;
	int incr_sign;
	u64 cas, incr;
	u32 flags, exptime, bytes;
	bool noreply = false;
	u8 *data = NULL;
	bool done = false;
	int r;
	size_t saved_iov_cnt = fiber->iov_cnt;
	uintptr_t flush_delay = 0;
	size_t keys_count = 0;

	p = fiber->rbuf->data;
	pe = fiber->rbuf->data + fiber->rbuf->len;

	say_debug("memcached_dispatch '%.*s'", MIN((int)(pe - p), 40) , p);

#define STORE ({									\
	stats.cmd_set++;								\
	if (bytes > (1<<20)) {								\
		add_iov("SERVER_ERROR object too large for cache\r\n", 41);		\
	} else {									\
		u32 ret_code;								\
		if ((ret_code = store(txn, key, exptime, flags, bytes, data)) == 0) {	\
			stats.total_items++;						\
			add_iov("STORED\r\n", 8);					\
		} else {								\
			add_iov("SERVER_ERROR ", 13);					\
			add_iov(ERRCODE_DESC(error_codes, ret_code),			\
				strlen(ERRCODE_DESC(error_codes, ret_code)));		\
			add_iov("\r\n", 2);						\
		}									\
	}										\
})

	%%{
		action set {
			key = read_field(keys);
			STORE;
		}

		action add {
			key = read_field(keys);
			struct box_tuple *tuple = find(key);
			if (tuple != NULL && !expired(tuple))
				add_iov("NOT_STORED\r\n", 12);
			else
				STORE;
		}

		action replace {
			key = read_field(keys);
			struct box_tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple))
				add_iov("NOT_STORED\r\n", 12);
			else
				STORE;
		}

		action cas {
			key = read_field(keys);
			struct box_tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple))
				add_iov("NOT_FOUND\r\n", 11);
			else if (meta(tuple)->cas != cas)
				add_iov("EXISTS\r\n", 8);
			else
				STORE;
		}

		action append_prepend {
			struct tbuf *b;
			void *value;
			u32 value_len;

			key = read_field(keys);
			struct box_tuple *tuple = find(key);
			if (tuple == NULL || tuple->flags & GHOST) {
				add_iov("NOT_STORED\r\n", 12);
			} else {
				value = tuple_field(tuple, 3);
				value_len = load_varint32(&value);
				b = tbuf_alloc(fiber->pool);
				if (append) {
					tbuf_append(b, value, value_len);
					tbuf_append(b, data, bytes);
				} else {
					tbuf_append(b, data, bytes);
					tbuf_append(b, value, value_len);
				}

				bytes += value_len;
				data = b->data;
				STORE;
			}
		}

		action incr_decr {
			struct meta *m;
			struct tbuf *b;
			void *field;
			u32 value_len;
			u64 value;

			key = read_field(keys);
			struct box_tuple *tuple = find(key);
			if (tuple == NULL || tuple->flags & GHOST || expired(tuple)) {
				add_iov("NOT_FOUND\r\n", 11);
			} else {
				m = meta(tuple);
				field = tuple_field(tuple, 3);
				value_len = load_varint32(&field);

				if (is_numeric(field, value_len)) {
					value = natoq(field, field + value_len);

					if (incr_sign > 0) {
						value += incr;
					} else {
						if (incr > value)
							value = 0;
						else
							value -= incr;
					}

					exptime = m->exptime;
					flags = m->flags;

					b = tbuf_alloc(fiber->pool);
					tbuf_printf(b, "%"PRIu64, value);
					data = b->data;
					bytes = b->len;

					stats.cmd_set++;
					if (store(txn, key, exptime, flags, bytes, data) == 0) {
						stats.total_items++;
						add_iov(b->data, b->len);
						add_iov("\r\n", 2);
					} else {
						add_iov("SERVER_ERROR\r\n", 14);
					}
				} else {
					add_iov("CLIENT_ERROR cannot increment or decrement non-numeric value\r\n", 62);
				}
			}

		}

		action delete {
			key = read_field(keys);
			struct box_tuple *tuple = find(key);
			if (tuple == NULL || tuple->flags & GHOST || expired(tuple)) {
				add_iov("NOT_FOUND\r\n", 11);
			} else {
				u32 ret_code;
				if ((ret_code = delete(txn, key)) == 0)
					add_iov("DELETED\r\n", 9);
				else {
					add_iov("SERVER_ERROR ", 13);
					add_iov(ERRCODE_DESC(error_codes, ret_code),
						strlen(ERRCODE_DESC(error_codes,ret_code)));
					add_iov("\r\n", 2);
				}
			}
		}

		action get {
			txn->op = SELECT;
			fiber_register_cleanup((void *)txn_cleanup, txn);
			stat_collect(stat_base, MEMC_GET, 1);
			stats.cmd_get++;
			say_debug("ensuring space for %"PRI_SZ" keys", keys_count);
			iov_ensure(keys_count * 5 + 1);
			while (keys_count-- > 0) {
				struct box_tuple *tuple;
				struct meta *m;
				void *field;
				void *value;
				void *suffix;
				u32 key_len;
				u32 value_len;
				u32 suffix_len;
				u32 _l;

				key = read_field(keys);
				tuple = find(key);
				key_len = load_varint32(&key);

				if (tuple == NULL || tuple->flags & GHOST) {
					stat_collect(stat_base, MEMC_GET_MISS, 1);
					stats.get_misses++;
					continue;
				}

				field = tuple->data;

				/* skip key */
				_l = load_varint32(&field);
				field += _l;

				/* metainfo */
				_l = load_varint32(&field);
				m = field;
				field += _l;

				/* suffix */
				suffix_len = load_varint32(&field);
				suffix = field;
				field += suffix_len;

				/* value */
				value_len = load_varint32(&field);
				value = field;

				if (m->exptime > 0 && m->exptime < ev_now()) {
					stats.get_misses++;
					stat_collect(stat_base, MEMC_GET_MISS, 1);
					continue;
				} else {
					stats.get_hits++;
					stat_collect(stat_base, MEMC_GET_HIT, 1);
				}

				tuple_txn_ref(txn, tuple);

				if (show_cas) {
					struct tbuf *b = tbuf_alloc(fiber->pool);
					tbuf_printf(b, "VALUE %.*s %"PRIu32" %"PRIu32" %"PRIu64"\r\n", key_len, (u8 *)key, m->flags, value_len, m->cas);
					add_iov_unsafe(b->data, b->len);
					stats.bytes_written += b->len;
				} else {
					add_iov_unsafe("VALUE ", 6);
					add_iov_unsafe(key, key_len);
					add_iov_unsafe(suffix, suffix_len);
				}
				add_iov_unsafe(value, value_len);
				add_iov_unsafe("\r\n", 2);
				stats.bytes_written += value_len + 2;
			}
			add_iov_unsafe("END\r\n", 5);
			stats.bytes_written += 5;
		}

		action flush_all {
			if (flush_delay > 0) {
				struct fiber *f = fiber_create("flush_all", -1, -1, flush_all, (void *)flush_delay);
				if (f)
					fiber_call(f);
			} else
				flush_all((void *)0);
			add_iov("OK\r\n", 4);
		}

		action stats {
			print_stats();
		}

		action quit {
			return 0;
		}

		action fstart { fstart = p; }
		action key_start {
			fstart = p;
			for (; p < pe && *p != ' ' && *p != '\r' && *p != '\n'; p++);
			if ( *p == ' ' || *p == '\r' || *p == '\n') {
				write_varint32(keys, p - fstart);
				tbuf_append(keys, fstart, p - fstart);
				keys_count++;
				p--;
			} else
				p = fstart;
 		}


		printable = [^ \t\r\n];
		key = printable >key_start ;

		action exptime {
			exptime = natoq(fstart, p);
			if (exptime > 0 && exptime <= 60*60*24*30)
				exptime = exptime + ev_now();
		}
		exptime = digit+ >fstart %exptime;

		flags = digit+ >fstart %{flags = natoq(fstart, p);};
		bytes = digit+ >fstart %{bytes = natoq(fstart, p);};
		cas_value = digit+ >fstart %{cas = natoq(fstart, p);};
		incr_value = digit+ >fstart %{incr = natoq(fstart, p);};
		flush_delay = digit+ >fstart %{flush_delay = natoq(fstart, p);};

		action read_data {
			size_t parsed = p - (u8 *)fiber->rbuf->data;
			while (fiber->rbuf->len - parsed < bytes + 2) {
				if ((r = fiber_bread(fiber->rbuf, bytes + 2 - (pe - p))) <= 0) {
					say_debug("read returned %i, closing connection", r);
					return 0;
				}
			}

			p = fiber->rbuf->data + parsed;
			pe = fiber->rbuf->data + fiber->rbuf->len;

			data = p;

			if (strncmp((char *)(p + bytes), "\r\n", 2) == 0) {
				p += bytes + 2;
			} else {
				goto exit;
			}
		}

		action done {
			done = true;
			stats.bytes_read += p - (u8 *)fiber->rbuf->data;
			tbuf_peek(fiber->rbuf, p - (u8 *)fiber->rbuf->data);
		}

		eol = ("\r\n" | "\n") @{ p++; };
		spc = " "+;
		noreply = (spc "noreply" %{ noreply = true; })?;
		store_command_body = spc key spc flags spc exptime spc bytes noreply eol;

		set = ("set" store_command_body) @read_data @done @set;
		add = ("add" store_command_body) @read_data @done @add;
		replace = ("replace" store_command_body) @read_data @done @replace;
		append  = ("append"  %{append = true; } store_command_body) @read_data @done @append_prepend;
		prepend = ("prepend" %{append = false;} store_command_body) @read_data @done @append_prepend;
		cas = ("cas" spc key spc flags spc exptime spc bytes spc cas_value noreply spc?) eol @read_data @done @cas;


		get = "get" %{show_cas = false;} spc key (spc key)* spc? eol @done @get;
		gets = "gets" %{show_cas = true;} spc key (spc key)* spc? eol @done @get;
		delete = "delete" spc key (spc exptime)? noreply spc? eol @done @delete;
		incr = "incr" %{incr_sign = 1; } spc key spc incr_value noreply spc? eol @done @incr_decr;
		decr = "decr" %{incr_sign = -1;} spc key spc incr_value noreply spc? eol @done @incr_decr;

		stats = "stats" eol @done @stats;
		flush_all = "flush_all" (spc flush_delay)? noreply spc? eol @done @flush_all;
		quit = "quit" eol @done @quit;

	        main := set | cas | add | replace | append | prepend | get | gets | delete | incr | decr | stats | flush_all | quit;
	        #main := set;
		write init;
		write exec;
	}%%

	if (!done) {
		say_debug("parse failed after: `%.*s'", (int)(pe - p), p);
		if (pe - p > (1 << 20)) {
		exit:
			say_warn("memcached proto error");
			add_iov("ERROR\r\n", 7);
			stats.bytes_written += 7;
			return -1;
		}
		char *r;
		if ((r = memmem(p, pe - p, "\r\n", 2)) != NULL) {
			tbuf_peek(fiber->rbuf, r + 2 - (char *)fiber->rbuf->data);
			add_iov("CLIENT_ERROR bad command line format\r\n", 38);
			return 1;
		}
		return 0;
	}

	if (noreply) {
		fiber->iov_cnt = saved_iov_cnt;
		fiber->iov->len = saved_iov_cnt * sizeof(struct iovec);
	}

	return 1;
}

void
memcached_handler(void *_data __unused__)
{
	struct box_txn *txn;
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
		txn = txn_alloc(BOX_QUIET);
		p = memcached_dispatch(txn);
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

void
memcached_init(void)
{
	stat_base = stat_register(memcached_stat_strs, memcached_stat_MAX);
}

void
memcached_expire(void *data __unused__)
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
			struct box_txn *txn = txn_alloc(BOX_QUIET);
			delete(txn, read_field(keys_to_delete));
			expired_keys++;
		}
		stat_collect(stat_base, MEMC_EXPIRED_KEYS, expired_keys);

		fiber_gc();

		double delay = (double)cfg.memcached_expire_per_loop * cfg.memcached_expire_full_sweep / (map->size + 1);
		if (delay > 1)
			delay = 1;
		fiber_sleep(delay);
	}
}

/*
 * Local Variables:
 * mode: c
 * End:
 * vim: syntax=c
 */
