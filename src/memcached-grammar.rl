/*
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
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
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

%%{
	machine memcached;
	write data;
}%%

static int __attribute__((noinline))
memcached_dispatch(struct ev_io *coio, struct iobuf *iobuf)
{
	int cs;
	char *p, *pe;
	char *fstart;
	struct tbuf *keys = tbuf_alloc(fiber->gc_pool);
	void *key;
	bool append, show_cas;
	int incr_sign;
	u64 cas, incr;
	u32 flags, exptime, bytes;
	bool noreply = false;
	char *data = NULL;
	bool done = false;
	uintptr_t flush_delay = 0;
	size_t keys_count = 0;
	struct ibuf *in = &iobuf->in;
	struct obuf *out = &iobuf->out;
	/* Savepoint for 'noreply' */
	struct obuf_svp obuf_svp = obuf_create_svp(out);

	p = in->pos;
	pe = in->end;

	say_debug("memcached_dispatch '%.*s'", MIN((int)(pe - p), 40) , p);

	%%{
		action set {
			key = read_field(keys);
			STORE;
		}

		action add {
			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple != NULL && !expired(tuple))
				obuf_dup(out, "NOT_STORED\r\n", 12);
			else
				STORE;
		}

		action replace {
			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple))
				obuf_dup(out, "NOT_STORED\r\n", 12);
			else
				STORE;
		}

		action cas {
			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple))
				obuf_dup(out, "NOT_FOUND\r\n", 11);
			else if (meta(tuple)->cas != cas)
				obuf_dup(out, "EXISTS\r\n", 8);
			else
				STORE;
		}

		action append_prepend {
			struct tbuf *b;
			const void *value;
			u32 value_len;

			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple == NULL) {
				obuf_dup(out, "NOT_STORED\r\n", 12);
			} else {
				value = tuple_field(tuple, 3);
				value_len = load_varint32(&value);
				b = tbuf_alloc(fiber->gc_pool);
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
			const void *field;
			u32 value_len;
			u64 value;

			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple)) {
				obuf_dup(out, "NOT_FOUND\r\n", 11);
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

					b = tbuf_alloc(fiber->gc_pool);
					tbuf_printf(b, "%"PRIu64, value);
					data = b->data;
					bytes = b->size;

					stats.cmd_set++;
					@try {
						store(key, exptime, flags, bytes, data);
						stats.total_items++;
						obuf_dup(out, b->data, b->size);
						obuf_dup(out, "\r\n", 2);
					}
					@catch (ClientError *e) {
						obuf_dup(out, "SERVER_ERROR ", 13);
						obuf_dup(out, e->errmsg, strlen(e->errmsg));
						obuf_dup(out, "\r\n", 2);
					}
				} else {
					obuf_dup(out, "CLIENT_ERROR cannot increment or decrement non-numeric value\r\n", 62);
				}
			}

		}

		action delete {
			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple)) {
				obuf_dup(out, "NOT_FOUND\r\n", 11);
			} else {
				@try {
					delete(key);
					obuf_dup(out, "DELETED\r\n", 9);
				}
				@catch (ClientError *e) {
					obuf_dup(out, "SERVER_ERROR ", 13);
					obuf_dup(out, e->errmsg, strlen(e->errmsg));
					obuf_dup(out, "\r\n", 2);
				}
			}
		}

		action get {
			@try {
				memcached_get(out, keys_count, keys, show_cas);
			} @catch (ClientError *e) {
				obuf_rollback_to_svp(out, &obuf_svp);
				obuf_dup(out, "SERVER_ERROR ", 13);
				obuf_dup(out, e->errmsg, strlen(e->errmsg));
				obuf_dup(out, "\r\n", 2);
			}
		}

		action flush_all {
			struct fiber *f = fiber_create("flush_all", flush_all);
			fiber_call(f, flush_delay);
			obuf_dup(out, "OK\r\n", 4);
		}

		action stats {
			print_stats(out);
		}

		action quit {
			return -1;
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
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				size_t to_read = bytes + 2 - (pe - p);
				if (coio_bread(coio, in, to_read) < to_read)
					return -1; /* premature EOF */
			}
			/*
			 * Buffered read may have reallocated the
			 * buffer.
			 */
			p = in->pos + parsed;
			pe = in->end;

			data = p;

			if (strncmp((char *)(p + bytes), "\r\n", 2) == 0) {
				p += bytes + 2;
			} else {
				goto exit;
			}
		}

		action done {
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}

		eol = ("\r\n" | "\n") @{ p++; };
		spc = " "+;
		noreply = (spc "noreply"i %{ noreply = true; })?;
		store_command_body = spc key spc flags spc exptime spc bytes noreply eol;

		set = ("set"i store_command_body) @read_data @done @set;
		add = ("add"i store_command_body) @read_data @done @add;
		replace = ("replace"i store_command_body) @read_data @done @replace;
		append  = ("append"i  %{append = true; } store_command_body) @read_data @done @append_prepend;
		prepend = ("prepend"i %{append = false;} store_command_body) @read_data @done @append_prepend;
		cas = ("cas"i spc key spc flags spc exptime spc bytes spc cas_value noreply spc?) eol @read_data @done @cas;


		get = "get"i %{show_cas = false;} spc key (spc key)* spc? eol @done @get;
		gets = "gets"i %{show_cas = true;} spc key (spc key)* spc? eol @done @get;
		delete = "delete"i spc key (spc exptime)? noreply spc? eol @done @delete;
		incr = "incr"i %{incr_sign = 1; } spc key spc incr_value noreply spc? eol @done @incr_decr;
		decr = "decr"i %{incr_sign = -1;} spc key spc incr_value noreply spc? eol @done @incr_decr;

		stats = "stats"i eol @done @stats;
		flush_all = "flush_all"i (spc flush_delay)? noreply spc? eol @done @flush_all;
		quit = "quit"i eol @done @quit;

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
			obuf_dup(out, "ERROR\r\n", 7);
			stats.bytes_written += 7;
			return -1;
		}
		char *r;
		if ((r = memmem(p, pe - p, "\r\n", 2)) != NULL) {
			in->pos = r + 2;
			obuf_dup(out, "CLIENT_ERROR bad command line format\r\n", 38);
			return 1;
		}
		return 0;
	}

	if (noreply) {
		obuf_rollback_to_svp(out, &obuf_svp);
	}
	return 1;
}

/*
 * Local Variables:
 * mode: c
 * End:
 * vim: syntax=objc
 */
