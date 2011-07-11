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

%%{
	machine memcached;
	write data;
}%%

static int __attribute__((noinline))
memcached_dispatch()
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
					@try {
						store(key, exptime, flags, bytes, data);
						stats.total_items++;
						add_iov(b->data, b->len);
						add_iov("\r\n", 2);
					}
					@catch (ClientError *e) {
						add_iov("SERVER_ERROR ", 13);
						add_iov(e->errmsg, strlen(e->errmsg));
						add_iov("\r\n", 2);
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
				@try {
					delete(key);
					add_iov("DELETED\r\n", 9);
				}
				@catch (ClientError *e) {
					add_iov("SERVER_ERROR ", 13);
					add_iov(e->errmsg, strlen(e->errmsg));
					add_iov("\r\n", 2);
				}
			}
		}

		action get {
			struct box_txn *txn = txn_alloc(BOX_QUIET);
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

/*
 * Local Variables:
 * mode: c
 * End:
 * vim: syntax=objc
 */
