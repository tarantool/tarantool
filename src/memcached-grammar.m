
#line 1 "src/memcached-grammar.rl"
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


#line 34 "src/memcached-grammar.m"
static const int memcached_start = 1;
static const int memcached_first_final = 197;
static const int memcached_error = 0;

static const int memcached_en_main = 1;


#line 33 "src/memcached-grammar.rl"


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

	
#line 73 "src/memcached-grammar.m"
	{
	cs = memcached_start;
	}

#line 78 "src/memcached-grammar.m"
	{
	if ( p == pe )
		goto _test_eof;
	switch ( cs )
	{
case 1:
	switch( (*p) ) {
		case 65: goto st2;
		case 67: goto st44;
		case 68: goto st67;
		case 70: goto st103;
		case 71: goto st124;
		case 73: goto st132;
		case 80: goto st136;
		case 81: goto st143;
		case 82: goto st148;
		case 83: goto st172;
		case 97: goto st2;
		case 99: goto st44;
		case 100: goto st67;
		case 102: goto st103;
		case 103: goto st124;
		case 105: goto st132;
		case 112: goto st136;
		case 113: goto st143;
		case 114: goto st148;
		case 115: goto st172;
	}
	goto st0;
st0:
cs = 0;
	goto _out;
st2:
	if ( ++p == pe )
		goto _test_eof2;
case 2:
	switch( (*p) ) {
		case 68: goto st3;
		case 80: goto st22;
		case 100: goto st3;
		case 112: goto st22;
	}
	goto st0;
st3:
	if ( ++p == pe )
		goto _test_eof3;
case 3:
	switch( (*p) ) {
		case 68: goto st4;
		case 100: goto st4;
	}
	goto st0;
st4:
	if ( ++p == pe )
		goto _test_eof4;
case 4:
	if ( (*p) == 32 )
		goto st5;
	goto st0;
st5:
	if ( ++p == pe )
		goto _test_eof5;
case 5:
	switch( (*p) ) {
		case 13: goto st0;
		case 32: goto st5;
	}
	if ( 9 <= (*p) && (*p) <= 10 )
		goto st0;
	goto tr15;
tr15:
#line 223 "src/memcached-grammar.rl"
	{
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
	goto st6;
st6:
	if ( ++p == pe )
		goto _test_eof6;
case 6:
#line 167 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st7;
	goto st0;
st7:
	if ( ++p == pe )
		goto _test_eof7;
case 7:
	if ( (*p) == 32 )
		goto st7;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr17;
	goto st0;
tr17:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st8;
st8:
	if ( ++p == pe )
		goto _test_eof8;
case 8:
#line 188 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto tr18;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st8;
	goto st0;
tr18:
#line 246 "src/memcached-grammar.rl"
	{flags = natoq(fstart, p);}
	goto st9;
st9:
	if ( ++p == pe )
		goto _test_eof9;
case 9:
#line 202 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st9;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr21;
	goto st0;
tr21:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st10;
st10:
	if ( ++p == pe )
		goto _test_eof10;
case 10:
#line 216 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto tr22;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st10;
	goto st0;
tr22:
#line 239 "src/memcached-grammar.rl"
	{
			exptime = natoq(fstart, p);
			if (exptime > 0 && exptime <= 60*60*24*30)
				exptime = exptime + ev_now();
		}
	goto st11;
st11:
	if ( ++p == pe )
		goto _test_eof11;
case 11:
#line 234 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st11;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr25;
	goto st0;
tr25:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st12;
st12:
	if ( ++p == pe )
		goto _test_eof12;
case 12:
#line 248 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr26;
		case 13: goto tr27;
		case 32: goto tr28;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st12;
	goto st0;
tr26:
#line 247 "src/memcached-grammar.rl"
	{bytes = natoq(fstart, p);}
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 68 "src/memcached-grammar.rl"
	{
			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple != NULL && !expired(tuple))
				obuf_dup(out, "NOT_STORED\r\n", 12);
			else
				STORE;
		}
	goto st197;
tr30:
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 68 "src/memcached-grammar.rl"
	{
			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple != NULL && !expired(tuple))
				obuf_dup(out, "NOT_STORED\r\n", 12);
			else
				STORE;
		}
	goto st197;
tr39:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 68 "src/memcached-grammar.rl"
	{
			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple != NULL && !expired(tuple))
				obuf_dup(out, "NOT_STORED\r\n", 12);
			else
				STORE;
		}
	goto st197;
tr58:
#line 247 "src/memcached-grammar.rl"
	{bytes = natoq(fstart, p);}
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 97 "src/memcached-grammar.rl"
	{
			struct tbuf *b;
			void *value;
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
	goto st197;
tr62:
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 97 "src/memcached-grammar.rl"
	{
			struct tbuf *b;
			void *value;
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
	goto st197;
tr71:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 97 "src/memcached-grammar.rl"
	{
			struct tbuf *b;
			void *value;
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
	goto st197;
tr91:
#line 248 "src/memcached-grammar.rl"
	{cas = natoq(fstart, p);}
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 86 "src/memcached-grammar.rl"
	{
			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple))
				obuf_dup(out, "NOT_FOUND\r\n", 11);
			else if (meta(tuple)->cas != cas)
				obuf_dup(out, "EXISTS\r\n", 8);
			else
				STORE;
		}
	goto st197;
tr95:
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 86 "src/memcached-grammar.rl"
	{
			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple))
				obuf_dup(out, "NOT_FOUND\r\n", 11);
			else if (meta(tuple)->cas != cas)
				obuf_dup(out, "EXISTS\r\n", 8);
			else
				STORE;
		}
	goto st197;
tr105:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 86 "src/memcached-grammar.rl"
	{
			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple))
				obuf_dup(out, "NOT_FOUND\r\n", 11);
			else if (meta(tuple)->cas != cas)
				obuf_dup(out, "EXISTS\r\n", 8);
			else
				STORE;
		}
	goto st197;
tr118:
#line 249 "src/memcached-grammar.rl"
	{incr = natoq(fstart, p);}
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 124 "src/memcached-grammar.rl"
	{
			struct meta *m;
			struct tbuf *b;
			void *field;
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
	goto st197;
tr122:
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 124 "src/memcached-grammar.rl"
	{
			struct meta *m;
			struct tbuf *b;
			void *field;
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
	goto st197;
tr132:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 124 "src/memcached-grammar.rl"
	{
			struct meta *m;
			struct tbuf *b;
			void *field;
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
	goto st197;
tr141:
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 179 "src/memcached-grammar.rl"
	{
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
	goto st197;
tr146:
#line 239 "src/memcached-grammar.rl"
	{
			exptime = natoq(fstart, p);
			if (exptime > 0 && exptime <= 60*60*24*30)
				exptime = exptime + ev_now();
		}
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 179 "src/memcached-grammar.rl"
	{
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
	goto st197;
tr157:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 179 "src/memcached-grammar.rl"
	{
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
	goto st197;
tr169:
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 208 "src/memcached-grammar.rl"
	{
			struct fiber *f = fiber_create("flush_all", flush_all);
			fiber_call(f, flush_delay);
			obuf_dup(out, "OK\r\n", 4);
		}
	goto st197;
tr174:
#line 250 "src/memcached-grammar.rl"
	{flush_delay = natoq(fstart, p);}
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 208 "src/memcached-grammar.rl"
	{
			struct fiber *f = fiber_create("flush_all", flush_all);
			fiber_call(f, flush_delay);
			obuf_dup(out, "OK\r\n", 4);
		}
	goto st197;
tr185:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 208 "src/memcached-grammar.rl"
	{
			struct fiber *f = fiber_create("flush_all", flush_all);
			fiber_call(f, flush_delay);
			obuf_dup(out, "OK\r\n", 4);
		}
	goto st197;
tr195:
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 197 "src/memcached-grammar.rl"
	{
			@try {
				memcached_get(out, keys_count, keys, show_cas);
			} @catch (ClientError *e) {
				obuf_rollback_to_svp(out, &obuf_svp);
				obuf_dup(out, "SERVER_ERROR ", 13);
				obuf_dup(out, e->errmsg, strlen(e->errmsg));
				obuf_dup(out, "\r\n", 2);
			}
		}
	goto st197;
tr213:
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 218 "src/memcached-grammar.rl"
	{
			return -1;
		}
	goto st197;
tr233:
#line 247 "src/memcached-grammar.rl"
	{bytes = natoq(fstart, p);}
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 77 "src/memcached-grammar.rl"
	{
			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple))
				obuf_dup(out, "NOT_STORED\r\n", 12);
			else
				STORE;
		}
	goto st197;
tr237:
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 77 "src/memcached-grammar.rl"
	{
			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple))
				obuf_dup(out, "NOT_STORED\r\n", 12);
			else
				STORE;
		}
	goto st197;
tr246:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 77 "src/memcached-grammar.rl"
	{
			key = read_field(keys);
			struct tuple *tuple = find(key);
			if (tuple == NULL || expired(tuple))
				obuf_dup(out, "NOT_STORED\r\n", 12);
			else
				STORE;
		}
	goto st197;
tr263:
#line 247 "src/memcached-grammar.rl"
	{bytes = natoq(fstart, p);}
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 63 "src/memcached-grammar.rl"
	{
			key = read_field(keys);
			STORE;
		}
	goto st197;
tr267:
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 63 "src/memcached-grammar.rl"
	{
			key = read_field(keys);
			STORE;
		}
	goto st197;
tr276:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 252 "src/memcached-grammar.rl"
	{
			size_t parsed = p - in->pos;
			while (ibuf_size(in) - parsed < bytes + 2) {
				if (coio_bread(coio, in, bytes + 2 - (pe - p)) <= 0)
					return -1;
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
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 63 "src/memcached-grammar.rl"
	{
			key = read_field(keys);
			STORE;
		}
	goto st197;
tr281:
#line 280 "src/memcached-grammar.rl"
	{ p++; }
#line 274 "src/memcached-grammar.rl"
	{
			done = true;
			stats.bytes_read += p - in->pos;
			in->pos = p;
		}
#line 214 "src/memcached-grammar.rl"
	{
			print_stats(out);
		}
	goto st197;
st197:
	if ( ++p == pe )
		goto _test_eof197;
case 197:
#line 1336 "src/memcached-grammar.m"
	goto st0;
tr27:
#line 247 "src/memcached-grammar.rl"
	{bytes = natoq(fstart, p);}
	goto st13;
tr40:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
	goto st13;
st13:
	if ( ++p == pe )
		goto _test_eof13;
case 13:
#line 1350 "src/memcached-grammar.m"
	if ( (*p) == 10 )
		goto tr30;
	goto st0;
tr28:
#line 247 "src/memcached-grammar.rl"
	{bytes = natoq(fstart, p);}
	goto st14;
st14:
	if ( ++p == pe )
		goto _test_eof14;
case 14:
#line 1362 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 32: goto st14;
		case 78: goto st15;
		case 110: goto st15;
	}
	goto st0;
st15:
	if ( ++p == pe )
		goto _test_eof15;
case 15:
	switch( (*p) ) {
		case 79: goto st16;
		case 111: goto st16;
	}
	goto st0;
st16:
	if ( ++p == pe )
		goto _test_eof16;
case 16:
	switch( (*p) ) {
		case 82: goto st17;
		case 114: goto st17;
	}
	goto st0;
st17:
	if ( ++p == pe )
		goto _test_eof17;
case 17:
	switch( (*p) ) {
		case 69: goto st18;
		case 101: goto st18;
	}
	goto st0;
st18:
	if ( ++p == pe )
		goto _test_eof18;
case 18:
	switch( (*p) ) {
		case 80: goto st19;
		case 112: goto st19;
	}
	goto st0;
st19:
	if ( ++p == pe )
		goto _test_eof19;
case 19:
	switch( (*p) ) {
		case 76: goto st20;
		case 108: goto st20;
	}
	goto st0;
st20:
	if ( ++p == pe )
		goto _test_eof20;
case 20:
	switch( (*p) ) {
		case 89: goto st21;
		case 121: goto st21;
	}
	goto st0;
st21:
	if ( ++p == pe )
		goto _test_eof21;
case 21:
	switch( (*p) ) {
		case 10: goto tr39;
		case 13: goto tr40;
	}
	goto st0;
st22:
	if ( ++p == pe )
		goto _test_eof22;
case 22:
	switch( (*p) ) {
		case 80: goto st23;
		case 112: goto st23;
	}
	goto st0;
st23:
	if ( ++p == pe )
		goto _test_eof23;
case 23:
	switch( (*p) ) {
		case 69: goto st24;
		case 101: goto st24;
	}
	goto st0;
st24:
	if ( ++p == pe )
		goto _test_eof24;
case 24:
	switch( (*p) ) {
		case 78: goto st25;
		case 110: goto st25;
	}
	goto st0;
st25:
	if ( ++p == pe )
		goto _test_eof25;
case 25:
	switch( (*p) ) {
		case 68: goto st26;
		case 100: goto st26;
	}
	goto st0;
st26:
	if ( ++p == pe )
		goto _test_eof26;
case 26:
	if ( (*p) == 32 )
		goto tr45;
	goto st0;
tr45:
#line 288 "src/memcached-grammar.rl"
	{append = true; }
	goto st27;
tr209:
#line 289 "src/memcached-grammar.rl"
	{append = false;}
	goto st27;
st27:
	if ( ++p == pe )
		goto _test_eof27;
case 27:
#line 1487 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 13: goto st0;
		case 32: goto st27;
	}
	if ( 9 <= (*p) && (*p) <= 10 )
		goto st0;
	goto tr46;
tr46:
#line 223 "src/memcached-grammar.rl"
	{
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
	goto st28;
st28:
	if ( ++p == pe )
		goto _test_eof28;
case 28:
#line 1513 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st29;
	goto st0;
st29:
	if ( ++p == pe )
		goto _test_eof29;
case 29:
	if ( (*p) == 32 )
		goto st29;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr49;
	goto st0;
tr49:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st30;
st30:
	if ( ++p == pe )
		goto _test_eof30;
case 30:
#line 1534 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto tr50;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st30;
	goto st0;
tr50:
#line 246 "src/memcached-grammar.rl"
	{flags = natoq(fstart, p);}
	goto st31;
st31:
	if ( ++p == pe )
		goto _test_eof31;
case 31:
#line 1548 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st31;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr53;
	goto st0;
tr53:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st32;
st32:
	if ( ++p == pe )
		goto _test_eof32;
case 32:
#line 1562 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto tr54;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st32;
	goto st0;
tr54:
#line 239 "src/memcached-grammar.rl"
	{
			exptime = natoq(fstart, p);
			if (exptime > 0 && exptime <= 60*60*24*30)
				exptime = exptime + ev_now();
		}
	goto st33;
st33:
	if ( ++p == pe )
		goto _test_eof33;
case 33:
#line 1580 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st33;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr57;
	goto st0;
tr57:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st34;
st34:
	if ( ++p == pe )
		goto _test_eof34;
case 34:
#line 1594 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr58;
		case 13: goto tr59;
		case 32: goto tr60;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st34;
	goto st0;
tr59:
#line 247 "src/memcached-grammar.rl"
	{bytes = natoq(fstart, p);}
	goto st35;
tr72:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
	goto st35;
st35:
	if ( ++p == pe )
		goto _test_eof35;
case 35:
#line 1615 "src/memcached-grammar.m"
	if ( (*p) == 10 )
		goto tr62;
	goto st0;
tr60:
#line 247 "src/memcached-grammar.rl"
	{bytes = natoq(fstart, p);}
	goto st36;
st36:
	if ( ++p == pe )
		goto _test_eof36;
case 36:
#line 1627 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 32: goto st36;
		case 78: goto st37;
		case 110: goto st37;
	}
	goto st0;
st37:
	if ( ++p == pe )
		goto _test_eof37;
case 37:
	switch( (*p) ) {
		case 79: goto st38;
		case 111: goto st38;
	}
	goto st0;
st38:
	if ( ++p == pe )
		goto _test_eof38;
case 38:
	switch( (*p) ) {
		case 82: goto st39;
		case 114: goto st39;
	}
	goto st0;
st39:
	if ( ++p == pe )
		goto _test_eof39;
case 39:
	switch( (*p) ) {
		case 69: goto st40;
		case 101: goto st40;
	}
	goto st0;
st40:
	if ( ++p == pe )
		goto _test_eof40;
case 40:
	switch( (*p) ) {
		case 80: goto st41;
		case 112: goto st41;
	}
	goto st0;
st41:
	if ( ++p == pe )
		goto _test_eof41;
case 41:
	switch( (*p) ) {
		case 76: goto st42;
		case 108: goto st42;
	}
	goto st0;
st42:
	if ( ++p == pe )
		goto _test_eof42;
case 42:
	switch( (*p) ) {
		case 89: goto st43;
		case 121: goto st43;
	}
	goto st0;
st43:
	if ( ++p == pe )
		goto _test_eof43;
case 43:
	switch( (*p) ) {
		case 10: goto tr71;
		case 13: goto tr72;
	}
	goto st0;
st44:
	if ( ++p == pe )
		goto _test_eof44;
case 44:
	switch( (*p) ) {
		case 65: goto st45;
		case 97: goto st45;
	}
	goto st0;
st45:
	if ( ++p == pe )
		goto _test_eof45;
case 45:
	switch( (*p) ) {
		case 83: goto st46;
		case 115: goto st46;
	}
	goto st0;
st46:
	if ( ++p == pe )
		goto _test_eof46;
case 46:
	if ( (*p) == 32 )
		goto st47;
	goto st0;
st47:
	if ( ++p == pe )
		goto _test_eof47;
case 47:
	switch( (*p) ) {
		case 13: goto st0;
		case 32: goto st47;
	}
	if ( 9 <= (*p) && (*p) <= 10 )
		goto st0;
	goto tr76;
tr76:
#line 223 "src/memcached-grammar.rl"
	{
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
	goto st48;
st48:
	if ( ++p == pe )
		goto _test_eof48;
case 48:
#line 1751 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st49;
	goto st0;
st49:
	if ( ++p == pe )
		goto _test_eof49;
case 49:
	if ( (*p) == 32 )
		goto st49;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr78;
	goto st0;
tr78:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st50;
st50:
	if ( ++p == pe )
		goto _test_eof50;
case 50:
#line 1772 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto tr79;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st50;
	goto st0;
tr79:
#line 246 "src/memcached-grammar.rl"
	{flags = natoq(fstart, p);}
	goto st51;
st51:
	if ( ++p == pe )
		goto _test_eof51;
case 51:
#line 1786 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st51;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr82;
	goto st0;
tr82:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st52;
st52:
	if ( ++p == pe )
		goto _test_eof52;
case 52:
#line 1800 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto tr83;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st52;
	goto st0;
tr83:
#line 239 "src/memcached-grammar.rl"
	{
			exptime = natoq(fstart, p);
			if (exptime > 0 && exptime <= 60*60*24*30)
				exptime = exptime + ev_now();
		}
	goto st53;
st53:
	if ( ++p == pe )
		goto _test_eof53;
case 53:
#line 1818 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st53;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr86;
	goto st0;
tr86:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st54;
st54:
	if ( ++p == pe )
		goto _test_eof54;
case 54:
#line 1832 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto tr87;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st54;
	goto st0;
tr87:
#line 247 "src/memcached-grammar.rl"
	{bytes = natoq(fstart, p);}
	goto st55;
st55:
	if ( ++p == pe )
		goto _test_eof55;
case 55:
#line 1846 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st55;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr90;
	goto st0;
tr90:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st56;
st56:
	if ( ++p == pe )
		goto _test_eof56;
case 56:
#line 1860 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr91;
		case 13: goto tr92;
		case 32: goto tr93;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st56;
	goto st0;
tr106:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
	goto st57;
tr92:
#line 248 "src/memcached-grammar.rl"
	{cas = natoq(fstart, p);}
	goto st57;
st57:
	if ( ++p == pe )
		goto _test_eof57;
case 57:
#line 1881 "src/memcached-grammar.m"
	if ( (*p) == 10 )
		goto tr95;
	goto st0;
tr93:
#line 248 "src/memcached-grammar.rl"
	{cas = natoq(fstart, p);}
	goto st58;
st58:
	if ( ++p == pe )
		goto _test_eof58;
case 58:
#line 1893 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr95;
		case 13: goto st57;
		case 32: goto st58;
		case 78: goto st59;
		case 110: goto st59;
	}
	goto st0;
st59:
	if ( ++p == pe )
		goto _test_eof59;
case 59:
	switch( (*p) ) {
		case 79: goto st60;
		case 111: goto st60;
	}
	goto st0;
st60:
	if ( ++p == pe )
		goto _test_eof60;
case 60:
	switch( (*p) ) {
		case 82: goto st61;
		case 114: goto st61;
	}
	goto st0;
st61:
	if ( ++p == pe )
		goto _test_eof61;
case 61:
	switch( (*p) ) {
		case 69: goto st62;
		case 101: goto st62;
	}
	goto st0;
st62:
	if ( ++p == pe )
		goto _test_eof62;
case 62:
	switch( (*p) ) {
		case 80: goto st63;
		case 112: goto st63;
	}
	goto st0;
st63:
	if ( ++p == pe )
		goto _test_eof63;
case 63:
	switch( (*p) ) {
		case 76: goto st64;
		case 108: goto st64;
	}
	goto st0;
st64:
	if ( ++p == pe )
		goto _test_eof64;
case 64:
	switch( (*p) ) {
		case 89: goto st65;
		case 121: goto st65;
	}
	goto st0;
st65:
	if ( ++p == pe )
		goto _test_eof65;
case 65:
	switch( (*p) ) {
		case 10: goto tr105;
		case 13: goto tr106;
		case 32: goto tr107;
	}
	goto st0;
tr107:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
	goto st66;
st66:
	if ( ++p == pe )
		goto _test_eof66;
case 66:
#line 1974 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr95;
		case 13: goto st57;
		case 32: goto st66;
	}
	goto st0;
st67:
	if ( ++p == pe )
		goto _test_eof67;
case 67:
	switch( (*p) ) {
		case 69: goto st68;
		case 101: goto st68;
	}
	goto st0;
st68:
	if ( ++p == pe )
		goto _test_eof68;
case 68:
	switch( (*p) ) {
		case 67: goto st69;
		case 76: goto st85;
		case 99: goto st69;
		case 108: goto st85;
	}
	goto st0;
st69:
	if ( ++p == pe )
		goto _test_eof69;
case 69:
	switch( (*p) ) {
		case 82: goto st70;
		case 114: goto st70;
	}
	goto st0;
st70:
	if ( ++p == pe )
		goto _test_eof70;
case 70:
	if ( (*p) == 32 )
		goto tr113;
	goto st0;
tr113:
#line 297 "src/memcached-grammar.rl"
	{incr_sign = -1;}
	goto st71;
tr202:
#line 296 "src/memcached-grammar.rl"
	{incr_sign = 1; }
	goto st71;
st71:
	if ( ++p == pe )
		goto _test_eof71;
case 71:
#line 2029 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 13: goto st0;
		case 32: goto st71;
	}
	if ( 9 <= (*p) && (*p) <= 10 )
		goto st0;
	goto tr114;
tr114:
#line 223 "src/memcached-grammar.rl"
	{
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
	goto st72;
st72:
	if ( ++p == pe )
		goto _test_eof72;
case 72:
#line 2055 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st73;
	goto st0;
st73:
	if ( ++p == pe )
		goto _test_eof73;
case 73:
	if ( (*p) == 32 )
		goto st73;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr117;
	goto st0;
tr117:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st74;
st74:
	if ( ++p == pe )
		goto _test_eof74;
case 74:
#line 2076 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr118;
		case 13: goto tr119;
		case 32: goto tr120;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st74;
	goto st0;
tr133:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
	goto st75;
tr119:
#line 249 "src/memcached-grammar.rl"
	{incr = natoq(fstart, p);}
	goto st75;
st75:
	if ( ++p == pe )
		goto _test_eof75;
case 75:
#line 2097 "src/memcached-grammar.m"
	if ( (*p) == 10 )
		goto tr122;
	goto st0;
tr120:
#line 249 "src/memcached-grammar.rl"
	{incr = natoq(fstart, p);}
	goto st76;
st76:
	if ( ++p == pe )
		goto _test_eof76;
case 76:
#line 2109 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr122;
		case 13: goto st75;
		case 32: goto st76;
		case 78: goto st77;
		case 110: goto st77;
	}
	goto st0;
st77:
	if ( ++p == pe )
		goto _test_eof77;
case 77:
	switch( (*p) ) {
		case 79: goto st78;
		case 111: goto st78;
	}
	goto st0;
st78:
	if ( ++p == pe )
		goto _test_eof78;
case 78:
	switch( (*p) ) {
		case 82: goto st79;
		case 114: goto st79;
	}
	goto st0;
st79:
	if ( ++p == pe )
		goto _test_eof79;
case 79:
	switch( (*p) ) {
		case 69: goto st80;
		case 101: goto st80;
	}
	goto st0;
st80:
	if ( ++p == pe )
		goto _test_eof80;
case 80:
	switch( (*p) ) {
		case 80: goto st81;
		case 112: goto st81;
	}
	goto st0;
st81:
	if ( ++p == pe )
		goto _test_eof81;
case 81:
	switch( (*p) ) {
		case 76: goto st82;
		case 108: goto st82;
	}
	goto st0;
st82:
	if ( ++p == pe )
		goto _test_eof82;
case 82:
	switch( (*p) ) {
		case 89: goto st83;
		case 121: goto st83;
	}
	goto st0;
st83:
	if ( ++p == pe )
		goto _test_eof83;
case 83:
	switch( (*p) ) {
		case 10: goto tr132;
		case 13: goto tr133;
		case 32: goto tr134;
	}
	goto st0;
tr134:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
	goto st84;
st84:
	if ( ++p == pe )
		goto _test_eof84;
case 84:
#line 2190 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr122;
		case 13: goto st75;
		case 32: goto st84;
	}
	goto st0;
st85:
	if ( ++p == pe )
		goto _test_eof85;
case 85:
	switch( (*p) ) {
		case 69: goto st86;
		case 101: goto st86;
	}
	goto st0;
st86:
	if ( ++p == pe )
		goto _test_eof86;
case 86:
	switch( (*p) ) {
		case 84: goto st87;
		case 116: goto st87;
	}
	goto st0;
st87:
	if ( ++p == pe )
		goto _test_eof87;
case 87:
	switch( (*p) ) {
		case 69: goto st88;
		case 101: goto st88;
	}
	goto st0;
st88:
	if ( ++p == pe )
		goto _test_eof88;
case 88:
	if ( (*p) == 32 )
		goto st89;
	goto st0;
st89:
	if ( ++p == pe )
		goto _test_eof89;
case 89:
	switch( (*p) ) {
		case 13: goto st0;
		case 32: goto st89;
	}
	if ( 9 <= (*p) && (*p) <= 10 )
		goto st0;
	goto tr140;
tr140:
#line 223 "src/memcached-grammar.rl"
	{
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
	goto st90;
st90:
	if ( ++p == pe )
		goto _test_eof90;
case 90:
#line 2260 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr141;
		case 13: goto st91;
		case 32: goto st92;
	}
	goto st0;
tr147:
#line 239 "src/memcached-grammar.rl"
	{
			exptime = natoq(fstart, p);
			if (exptime > 0 && exptime <= 60*60*24*30)
				exptime = exptime + ev_now();
		}
	goto st91;
tr158:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
	goto st91;
st91:
	if ( ++p == pe )
		goto _test_eof91;
case 91:
#line 2283 "src/memcached-grammar.m"
	if ( (*p) == 10 )
		goto tr141;
	goto st0;
st92:
	if ( ++p == pe )
		goto _test_eof92;
case 92:
	switch( (*p) ) {
		case 10: goto tr141;
		case 13: goto st91;
		case 32: goto st92;
		case 78: goto st95;
		case 110: goto st95;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr144;
	goto st0;
tr144:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st93;
st93:
	if ( ++p == pe )
		goto _test_eof93;
case 93:
#line 2309 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr146;
		case 13: goto tr147;
		case 32: goto tr148;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st93;
	goto st0;
tr148:
#line 239 "src/memcached-grammar.rl"
	{
			exptime = natoq(fstart, p);
			if (exptime > 0 && exptime <= 60*60*24*30)
				exptime = exptime + ev_now();
		}
	goto st94;
st94:
	if ( ++p == pe )
		goto _test_eof94;
case 94:
#line 2330 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr141;
		case 13: goto st91;
		case 32: goto st94;
		case 78: goto st95;
		case 110: goto st95;
	}
	goto st0;
st95:
	if ( ++p == pe )
		goto _test_eof95;
case 95:
	switch( (*p) ) {
		case 79: goto st96;
		case 111: goto st96;
	}
	goto st0;
st96:
	if ( ++p == pe )
		goto _test_eof96;
case 96:
	switch( (*p) ) {
		case 82: goto st97;
		case 114: goto st97;
	}
	goto st0;
st97:
	if ( ++p == pe )
		goto _test_eof97;
case 97:
	switch( (*p) ) {
		case 69: goto st98;
		case 101: goto st98;
	}
	goto st0;
st98:
	if ( ++p == pe )
		goto _test_eof98;
case 98:
	switch( (*p) ) {
		case 80: goto st99;
		case 112: goto st99;
	}
	goto st0;
st99:
	if ( ++p == pe )
		goto _test_eof99;
case 99:
	switch( (*p) ) {
		case 76: goto st100;
		case 108: goto st100;
	}
	goto st0;
st100:
	if ( ++p == pe )
		goto _test_eof100;
case 100:
	switch( (*p) ) {
		case 89: goto st101;
		case 121: goto st101;
	}
	goto st0;
st101:
	if ( ++p == pe )
		goto _test_eof101;
case 101:
	switch( (*p) ) {
		case 10: goto tr157;
		case 13: goto tr158;
		case 32: goto tr159;
	}
	goto st0;
tr159:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
	goto st102;
st102:
	if ( ++p == pe )
		goto _test_eof102;
case 102:
#line 2411 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr141;
		case 13: goto st91;
		case 32: goto st102;
	}
	goto st0;
st103:
	if ( ++p == pe )
		goto _test_eof103;
case 103:
	switch( (*p) ) {
		case 76: goto st104;
		case 108: goto st104;
	}
	goto st0;
st104:
	if ( ++p == pe )
		goto _test_eof104;
case 104:
	switch( (*p) ) {
		case 85: goto st105;
		case 117: goto st105;
	}
	goto st0;
st105:
	if ( ++p == pe )
		goto _test_eof105;
case 105:
	switch( (*p) ) {
		case 83: goto st106;
		case 115: goto st106;
	}
	goto st0;
st106:
	if ( ++p == pe )
		goto _test_eof106;
case 106:
	switch( (*p) ) {
		case 72: goto st107;
		case 104: goto st107;
	}
	goto st0;
st107:
	if ( ++p == pe )
		goto _test_eof107;
case 107:
	if ( (*p) == 95 )
		goto st108;
	goto st0;
st108:
	if ( ++p == pe )
		goto _test_eof108;
case 108:
	switch( (*p) ) {
		case 65: goto st109;
		case 97: goto st109;
	}
	goto st0;
st109:
	if ( ++p == pe )
		goto _test_eof109;
case 109:
	switch( (*p) ) {
		case 76: goto st110;
		case 108: goto st110;
	}
	goto st0;
st110:
	if ( ++p == pe )
		goto _test_eof110;
case 110:
	switch( (*p) ) {
		case 76: goto st111;
		case 108: goto st111;
	}
	goto st0;
st111:
	if ( ++p == pe )
		goto _test_eof111;
case 111:
	switch( (*p) ) {
		case 10: goto tr169;
		case 13: goto st112;
		case 32: goto st113;
	}
	goto st0;
tr186:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
	goto st112;
tr175:
#line 250 "src/memcached-grammar.rl"
	{flush_delay = natoq(fstart, p);}
	goto st112;
st112:
	if ( ++p == pe )
		goto _test_eof112;
case 112:
#line 2510 "src/memcached-grammar.m"
	if ( (*p) == 10 )
		goto tr169;
	goto st0;
st113:
	if ( ++p == pe )
		goto _test_eof113;
case 113:
	switch( (*p) ) {
		case 10: goto tr169;
		case 13: goto st112;
		case 32: goto st113;
		case 78: goto st116;
		case 110: goto st116;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr172;
	goto st0;
tr172:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st114;
st114:
	if ( ++p == pe )
		goto _test_eof114;
case 114:
#line 2536 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr174;
		case 13: goto tr175;
		case 32: goto tr176;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st114;
	goto st0;
tr176:
#line 250 "src/memcached-grammar.rl"
	{flush_delay = natoq(fstart, p);}
	goto st115;
st115:
	if ( ++p == pe )
		goto _test_eof115;
case 115:
#line 2553 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr169;
		case 13: goto st112;
		case 32: goto st115;
		case 78: goto st116;
		case 110: goto st116;
	}
	goto st0;
st116:
	if ( ++p == pe )
		goto _test_eof116;
case 116:
	switch( (*p) ) {
		case 79: goto st117;
		case 111: goto st117;
	}
	goto st0;
st117:
	if ( ++p == pe )
		goto _test_eof117;
case 117:
	switch( (*p) ) {
		case 82: goto st118;
		case 114: goto st118;
	}
	goto st0;
st118:
	if ( ++p == pe )
		goto _test_eof118;
case 118:
	switch( (*p) ) {
		case 69: goto st119;
		case 101: goto st119;
	}
	goto st0;
st119:
	if ( ++p == pe )
		goto _test_eof119;
case 119:
	switch( (*p) ) {
		case 80: goto st120;
		case 112: goto st120;
	}
	goto st0;
st120:
	if ( ++p == pe )
		goto _test_eof120;
case 120:
	switch( (*p) ) {
		case 76: goto st121;
		case 108: goto st121;
	}
	goto st0;
st121:
	if ( ++p == pe )
		goto _test_eof121;
case 121:
	switch( (*p) ) {
		case 89: goto st122;
		case 121: goto st122;
	}
	goto st0;
st122:
	if ( ++p == pe )
		goto _test_eof122;
case 122:
	switch( (*p) ) {
		case 10: goto tr185;
		case 13: goto tr186;
		case 32: goto tr187;
	}
	goto st0;
tr187:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
	goto st123;
st123:
	if ( ++p == pe )
		goto _test_eof123;
case 123:
#line 2634 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr169;
		case 13: goto st112;
		case 32: goto st123;
	}
	goto st0;
st124:
	if ( ++p == pe )
		goto _test_eof124;
case 124:
	switch( (*p) ) {
		case 69: goto st125;
		case 101: goto st125;
	}
	goto st0;
st125:
	if ( ++p == pe )
		goto _test_eof125;
case 125:
	switch( (*p) ) {
		case 84: goto st126;
		case 116: goto st126;
	}
	goto st0;
st126:
	if ( ++p == pe )
		goto _test_eof126;
case 126:
	switch( (*p) ) {
		case 32: goto tr191;
		case 83: goto st131;
		case 115: goto st131;
	}
	goto st0;
tr191:
#line 293 "src/memcached-grammar.rl"
	{show_cas = false;}
	goto st127;
tr198:
#line 294 "src/memcached-grammar.rl"
	{show_cas = true;}
	goto st127;
st127:
	if ( ++p == pe )
		goto _test_eof127;
case 127:
#line 2681 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 13: goto st0;
		case 32: goto st127;
	}
	if ( 9 <= (*p) && (*p) <= 10 )
		goto st0;
	goto tr193;
tr193:
#line 223 "src/memcached-grammar.rl"
	{
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
	goto st128;
st128:
	if ( ++p == pe )
		goto _test_eof128;
case 128:
#line 2707 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr195;
		case 13: goto st129;
		case 32: goto st130;
	}
	goto st0;
st129:
	if ( ++p == pe )
		goto _test_eof129;
case 129:
	if ( (*p) == 10 )
		goto tr195;
	goto st0;
st130:
	if ( ++p == pe )
		goto _test_eof130;
case 130:
	switch( (*p) ) {
		case 9: goto st0;
		case 10: goto tr195;
		case 13: goto st129;
		case 32: goto st130;
	}
	goto tr193;
st131:
	if ( ++p == pe )
		goto _test_eof131;
case 131:
	if ( (*p) == 32 )
		goto tr198;
	goto st0;
st132:
	if ( ++p == pe )
		goto _test_eof132;
case 132:
	switch( (*p) ) {
		case 78: goto st133;
		case 110: goto st133;
	}
	goto st0;
st133:
	if ( ++p == pe )
		goto _test_eof133;
case 133:
	switch( (*p) ) {
		case 67: goto st134;
		case 99: goto st134;
	}
	goto st0;
st134:
	if ( ++p == pe )
		goto _test_eof134;
case 134:
	switch( (*p) ) {
		case 82: goto st135;
		case 114: goto st135;
	}
	goto st0;
st135:
	if ( ++p == pe )
		goto _test_eof135;
case 135:
	if ( (*p) == 32 )
		goto tr202;
	goto st0;
st136:
	if ( ++p == pe )
		goto _test_eof136;
case 136:
	switch( (*p) ) {
		case 82: goto st137;
		case 114: goto st137;
	}
	goto st0;
st137:
	if ( ++p == pe )
		goto _test_eof137;
case 137:
	switch( (*p) ) {
		case 69: goto st138;
		case 101: goto st138;
	}
	goto st0;
st138:
	if ( ++p == pe )
		goto _test_eof138;
case 138:
	switch( (*p) ) {
		case 80: goto st139;
		case 112: goto st139;
	}
	goto st0;
st139:
	if ( ++p == pe )
		goto _test_eof139;
case 139:
	switch( (*p) ) {
		case 69: goto st140;
		case 101: goto st140;
	}
	goto st0;
st140:
	if ( ++p == pe )
		goto _test_eof140;
case 140:
	switch( (*p) ) {
		case 78: goto st141;
		case 110: goto st141;
	}
	goto st0;
st141:
	if ( ++p == pe )
		goto _test_eof141;
case 141:
	switch( (*p) ) {
		case 68: goto st142;
		case 100: goto st142;
	}
	goto st0;
st142:
	if ( ++p == pe )
		goto _test_eof142;
case 142:
	if ( (*p) == 32 )
		goto tr209;
	goto st0;
st143:
	if ( ++p == pe )
		goto _test_eof143;
case 143:
	switch( (*p) ) {
		case 85: goto st144;
		case 117: goto st144;
	}
	goto st0;
st144:
	if ( ++p == pe )
		goto _test_eof144;
case 144:
	switch( (*p) ) {
		case 73: goto st145;
		case 105: goto st145;
	}
	goto st0;
st145:
	if ( ++p == pe )
		goto _test_eof145;
case 145:
	switch( (*p) ) {
		case 84: goto st146;
		case 116: goto st146;
	}
	goto st0;
st146:
	if ( ++p == pe )
		goto _test_eof146;
case 146:
	switch( (*p) ) {
		case 10: goto tr213;
		case 13: goto st147;
	}
	goto st0;
st147:
	if ( ++p == pe )
		goto _test_eof147;
case 147:
	if ( (*p) == 10 )
		goto tr213;
	goto st0;
st148:
	if ( ++p == pe )
		goto _test_eof148;
case 148:
	switch( (*p) ) {
		case 69: goto st149;
		case 101: goto st149;
	}
	goto st0;
st149:
	if ( ++p == pe )
		goto _test_eof149;
case 149:
	switch( (*p) ) {
		case 80: goto st150;
		case 112: goto st150;
	}
	goto st0;
st150:
	if ( ++p == pe )
		goto _test_eof150;
case 150:
	switch( (*p) ) {
		case 76: goto st151;
		case 108: goto st151;
	}
	goto st0;
st151:
	if ( ++p == pe )
		goto _test_eof151;
case 151:
	switch( (*p) ) {
		case 65: goto st152;
		case 97: goto st152;
	}
	goto st0;
st152:
	if ( ++p == pe )
		goto _test_eof152;
case 152:
	switch( (*p) ) {
		case 67: goto st153;
		case 99: goto st153;
	}
	goto st0;
st153:
	if ( ++p == pe )
		goto _test_eof153;
case 153:
	switch( (*p) ) {
		case 69: goto st154;
		case 101: goto st154;
	}
	goto st0;
st154:
	if ( ++p == pe )
		goto _test_eof154;
case 154:
	if ( (*p) == 32 )
		goto st155;
	goto st0;
st155:
	if ( ++p == pe )
		goto _test_eof155;
case 155:
	switch( (*p) ) {
		case 13: goto st0;
		case 32: goto st155;
	}
	if ( 9 <= (*p) && (*p) <= 10 )
		goto st0;
	goto tr222;
tr222:
#line 223 "src/memcached-grammar.rl"
	{
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
	goto st156;
st156:
	if ( ++p == pe )
		goto _test_eof156;
case 156:
#line 2967 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st157;
	goto st0;
st157:
	if ( ++p == pe )
		goto _test_eof157;
case 157:
	if ( (*p) == 32 )
		goto st157;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr224;
	goto st0;
tr224:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st158;
st158:
	if ( ++p == pe )
		goto _test_eof158;
case 158:
#line 2988 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto tr225;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st158;
	goto st0;
tr225:
#line 246 "src/memcached-grammar.rl"
	{flags = natoq(fstart, p);}
	goto st159;
st159:
	if ( ++p == pe )
		goto _test_eof159;
case 159:
#line 3002 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st159;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr228;
	goto st0;
tr228:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st160;
st160:
	if ( ++p == pe )
		goto _test_eof160;
case 160:
#line 3016 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto tr229;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st160;
	goto st0;
tr229:
#line 239 "src/memcached-grammar.rl"
	{
			exptime = natoq(fstart, p);
			if (exptime > 0 && exptime <= 60*60*24*30)
				exptime = exptime + ev_now();
		}
	goto st161;
st161:
	if ( ++p == pe )
		goto _test_eof161;
case 161:
#line 3034 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st161;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr232;
	goto st0;
tr232:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st162;
st162:
	if ( ++p == pe )
		goto _test_eof162;
case 162:
#line 3048 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr233;
		case 13: goto tr234;
		case 32: goto tr235;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st162;
	goto st0;
tr234:
#line 247 "src/memcached-grammar.rl"
	{bytes = natoq(fstart, p);}
	goto st163;
tr247:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
	goto st163;
st163:
	if ( ++p == pe )
		goto _test_eof163;
case 163:
#line 3069 "src/memcached-grammar.m"
	if ( (*p) == 10 )
		goto tr237;
	goto st0;
tr235:
#line 247 "src/memcached-grammar.rl"
	{bytes = natoq(fstart, p);}
	goto st164;
st164:
	if ( ++p == pe )
		goto _test_eof164;
case 164:
#line 3081 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 32: goto st164;
		case 78: goto st165;
		case 110: goto st165;
	}
	goto st0;
st165:
	if ( ++p == pe )
		goto _test_eof165;
case 165:
	switch( (*p) ) {
		case 79: goto st166;
		case 111: goto st166;
	}
	goto st0;
st166:
	if ( ++p == pe )
		goto _test_eof166;
case 166:
	switch( (*p) ) {
		case 82: goto st167;
		case 114: goto st167;
	}
	goto st0;
st167:
	if ( ++p == pe )
		goto _test_eof167;
case 167:
	switch( (*p) ) {
		case 69: goto st168;
		case 101: goto st168;
	}
	goto st0;
st168:
	if ( ++p == pe )
		goto _test_eof168;
case 168:
	switch( (*p) ) {
		case 80: goto st169;
		case 112: goto st169;
	}
	goto st0;
st169:
	if ( ++p == pe )
		goto _test_eof169;
case 169:
	switch( (*p) ) {
		case 76: goto st170;
		case 108: goto st170;
	}
	goto st0;
st170:
	if ( ++p == pe )
		goto _test_eof170;
case 170:
	switch( (*p) ) {
		case 89: goto st171;
		case 121: goto st171;
	}
	goto st0;
st171:
	if ( ++p == pe )
		goto _test_eof171;
case 171:
	switch( (*p) ) {
		case 10: goto tr246;
		case 13: goto tr247;
	}
	goto st0;
st172:
	if ( ++p == pe )
		goto _test_eof172;
case 172:
	switch( (*p) ) {
		case 69: goto st173;
		case 84: goto st192;
		case 101: goto st173;
		case 116: goto st192;
	}
	goto st0;
st173:
	if ( ++p == pe )
		goto _test_eof173;
case 173:
	switch( (*p) ) {
		case 84: goto st174;
		case 116: goto st174;
	}
	goto st0;
st174:
	if ( ++p == pe )
		goto _test_eof174;
case 174:
	if ( (*p) == 32 )
		goto st175;
	goto st0;
st175:
	if ( ++p == pe )
		goto _test_eof175;
case 175:
	switch( (*p) ) {
		case 13: goto st0;
		case 32: goto st175;
	}
	if ( 9 <= (*p) && (*p) <= 10 )
		goto st0;
	goto tr252;
tr252:
#line 223 "src/memcached-grammar.rl"
	{
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
	goto st176;
st176:
	if ( ++p == pe )
		goto _test_eof176;
case 176:
#line 3207 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st177;
	goto st0;
st177:
	if ( ++p == pe )
		goto _test_eof177;
case 177:
	if ( (*p) == 32 )
		goto st177;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr254;
	goto st0;
tr254:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st178;
st178:
	if ( ++p == pe )
		goto _test_eof178;
case 178:
#line 3228 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto tr255;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st178;
	goto st0;
tr255:
#line 246 "src/memcached-grammar.rl"
	{flags = natoq(fstart, p);}
	goto st179;
st179:
	if ( ++p == pe )
		goto _test_eof179;
case 179:
#line 3242 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st179;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr258;
	goto st0;
tr258:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st180;
st180:
	if ( ++p == pe )
		goto _test_eof180;
case 180:
#line 3256 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto tr259;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st180;
	goto st0;
tr259:
#line 239 "src/memcached-grammar.rl"
	{
			exptime = natoq(fstart, p);
			if (exptime > 0 && exptime <= 60*60*24*30)
				exptime = exptime + ev_now();
		}
	goto st181;
st181:
	if ( ++p == pe )
		goto _test_eof181;
case 181:
#line 3274 "src/memcached-grammar.m"
	if ( (*p) == 32 )
		goto st181;
	if ( 48 <= (*p) && (*p) <= 57 )
		goto tr262;
	goto st0;
tr262:
#line 222 "src/memcached-grammar.rl"
	{ fstart = p; }
	goto st182;
st182:
	if ( ++p == pe )
		goto _test_eof182;
case 182:
#line 3288 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 10: goto tr263;
		case 13: goto tr264;
		case 32: goto tr265;
	}
	if ( 48 <= (*p) && (*p) <= 57 )
		goto st182;
	goto st0;
tr264:
#line 247 "src/memcached-grammar.rl"
	{bytes = natoq(fstart, p);}
	goto st183;
tr277:
#line 282 "src/memcached-grammar.rl"
	{ noreply = true; }
	goto st183;
st183:
	if ( ++p == pe )
		goto _test_eof183;
case 183:
#line 3309 "src/memcached-grammar.m"
	if ( (*p) == 10 )
		goto tr267;
	goto st0;
tr265:
#line 247 "src/memcached-grammar.rl"
	{bytes = natoq(fstart, p);}
	goto st184;
st184:
	if ( ++p == pe )
		goto _test_eof184;
case 184:
#line 3321 "src/memcached-grammar.m"
	switch( (*p) ) {
		case 32: goto st184;
		case 78: goto st185;
		case 110: goto st185;
	}
	goto st0;
st185:
	if ( ++p == pe )
		goto _test_eof185;
case 185:
	switch( (*p) ) {
		case 79: goto st186;
		case 111: goto st186;
	}
	goto st0;
st186:
	if ( ++p == pe )
		goto _test_eof186;
case 186:
	switch( (*p) ) {
		case 82: goto st187;
		case 114: goto st187;
	}
	goto st0;
st187:
	if ( ++p == pe )
		goto _test_eof187;
case 187:
	switch( (*p) ) {
		case 69: goto st188;
		case 101: goto st188;
	}
	goto st0;
st188:
	if ( ++p == pe )
		goto _test_eof188;
case 188:
	switch( (*p) ) {
		case 80: goto st189;
		case 112: goto st189;
	}
	goto st0;
st189:
	if ( ++p == pe )
		goto _test_eof189;
case 189:
	switch( (*p) ) {
		case 76: goto st190;
		case 108: goto st190;
	}
	goto st0;
st190:
	if ( ++p == pe )
		goto _test_eof190;
case 190:
	switch( (*p) ) {
		case 89: goto st191;
		case 121: goto st191;
	}
	goto st0;
st191:
	if ( ++p == pe )
		goto _test_eof191;
case 191:
	switch( (*p) ) {
		case 10: goto tr276;
		case 13: goto tr277;
	}
	goto st0;
st192:
	if ( ++p == pe )
		goto _test_eof192;
case 192:
	switch( (*p) ) {
		case 65: goto st193;
		case 97: goto st193;
	}
	goto st0;
st193:
	if ( ++p == pe )
		goto _test_eof193;
case 193:
	switch( (*p) ) {
		case 84: goto st194;
		case 116: goto st194;
	}
	goto st0;
st194:
	if ( ++p == pe )
		goto _test_eof194;
case 194:
	switch( (*p) ) {
		case 83: goto st195;
		case 115: goto st195;
	}
	goto st0;
st195:
	if ( ++p == pe )
		goto _test_eof195;
case 195:
	switch( (*p) ) {
		case 10: goto tr281;
		case 13: goto st196;
	}
	goto st0;
st196:
	if ( ++p == pe )
		goto _test_eof196;
case 196:
	if ( (*p) == 10 )
		goto tr281;
	goto st0;
	}
	_test_eof2: cs = 2; goto _test_eof; 
	_test_eof3: cs = 3; goto _test_eof; 
	_test_eof4: cs = 4; goto _test_eof; 
	_test_eof5: cs = 5; goto _test_eof; 
	_test_eof6: cs = 6; goto _test_eof; 
	_test_eof7: cs = 7; goto _test_eof; 
	_test_eof8: cs = 8; goto _test_eof; 
	_test_eof9: cs = 9; goto _test_eof; 
	_test_eof10: cs = 10; goto _test_eof; 
	_test_eof11: cs = 11; goto _test_eof; 
	_test_eof12: cs = 12; goto _test_eof; 
	_test_eof197: cs = 197; goto _test_eof; 
	_test_eof13: cs = 13; goto _test_eof; 
	_test_eof14: cs = 14; goto _test_eof; 
	_test_eof15: cs = 15; goto _test_eof; 
	_test_eof16: cs = 16; goto _test_eof; 
	_test_eof17: cs = 17; goto _test_eof; 
	_test_eof18: cs = 18; goto _test_eof; 
	_test_eof19: cs = 19; goto _test_eof; 
	_test_eof20: cs = 20; goto _test_eof; 
	_test_eof21: cs = 21; goto _test_eof; 
	_test_eof22: cs = 22; goto _test_eof; 
	_test_eof23: cs = 23; goto _test_eof; 
	_test_eof24: cs = 24; goto _test_eof; 
	_test_eof25: cs = 25; goto _test_eof; 
	_test_eof26: cs = 26; goto _test_eof; 
	_test_eof27: cs = 27; goto _test_eof; 
	_test_eof28: cs = 28; goto _test_eof; 
	_test_eof29: cs = 29; goto _test_eof; 
	_test_eof30: cs = 30; goto _test_eof; 
	_test_eof31: cs = 31; goto _test_eof; 
	_test_eof32: cs = 32; goto _test_eof; 
	_test_eof33: cs = 33; goto _test_eof; 
	_test_eof34: cs = 34; goto _test_eof; 
	_test_eof35: cs = 35; goto _test_eof; 
	_test_eof36: cs = 36; goto _test_eof; 
	_test_eof37: cs = 37; goto _test_eof; 
	_test_eof38: cs = 38; goto _test_eof; 
	_test_eof39: cs = 39; goto _test_eof; 
	_test_eof40: cs = 40; goto _test_eof; 
	_test_eof41: cs = 41; goto _test_eof; 
	_test_eof42: cs = 42; goto _test_eof; 
	_test_eof43: cs = 43; goto _test_eof; 
	_test_eof44: cs = 44; goto _test_eof; 
	_test_eof45: cs = 45; goto _test_eof; 
	_test_eof46: cs = 46; goto _test_eof; 
	_test_eof47: cs = 47; goto _test_eof; 
	_test_eof48: cs = 48; goto _test_eof; 
	_test_eof49: cs = 49; goto _test_eof; 
	_test_eof50: cs = 50; goto _test_eof; 
	_test_eof51: cs = 51; goto _test_eof; 
	_test_eof52: cs = 52; goto _test_eof; 
	_test_eof53: cs = 53; goto _test_eof; 
	_test_eof54: cs = 54; goto _test_eof; 
	_test_eof55: cs = 55; goto _test_eof; 
	_test_eof56: cs = 56; goto _test_eof; 
	_test_eof57: cs = 57; goto _test_eof; 
	_test_eof58: cs = 58; goto _test_eof; 
	_test_eof59: cs = 59; goto _test_eof; 
	_test_eof60: cs = 60; goto _test_eof; 
	_test_eof61: cs = 61; goto _test_eof; 
	_test_eof62: cs = 62; goto _test_eof; 
	_test_eof63: cs = 63; goto _test_eof; 
	_test_eof64: cs = 64; goto _test_eof; 
	_test_eof65: cs = 65; goto _test_eof; 
	_test_eof66: cs = 66; goto _test_eof; 
	_test_eof67: cs = 67; goto _test_eof; 
	_test_eof68: cs = 68; goto _test_eof; 
	_test_eof69: cs = 69; goto _test_eof; 
	_test_eof70: cs = 70; goto _test_eof; 
	_test_eof71: cs = 71; goto _test_eof; 
	_test_eof72: cs = 72; goto _test_eof; 
	_test_eof73: cs = 73; goto _test_eof; 
	_test_eof74: cs = 74; goto _test_eof; 
	_test_eof75: cs = 75; goto _test_eof; 
	_test_eof76: cs = 76; goto _test_eof; 
	_test_eof77: cs = 77; goto _test_eof; 
	_test_eof78: cs = 78; goto _test_eof; 
	_test_eof79: cs = 79; goto _test_eof; 
	_test_eof80: cs = 80; goto _test_eof; 
	_test_eof81: cs = 81; goto _test_eof; 
	_test_eof82: cs = 82; goto _test_eof; 
	_test_eof83: cs = 83; goto _test_eof; 
	_test_eof84: cs = 84; goto _test_eof; 
	_test_eof85: cs = 85; goto _test_eof; 
	_test_eof86: cs = 86; goto _test_eof; 
	_test_eof87: cs = 87; goto _test_eof; 
	_test_eof88: cs = 88; goto _test_eof; 
	_test_eof89: cs = 89; goto _test_eof; 
	_test_eof90: cs = 90; goto _test_eof; 
	_test_eof91: cs = 91; goto _test_eof; 
	_test_eof92: cs = 92; goto _test_eof; 
	_test_eof93: cs = 93; goto _test_eof; 
	_test_eof94: cs = 94; goto _test_eof; 
	_test_eof95: cs = 95; goto _test_eof; 
	_test_eof96: cs = 96; goto _test_eof; 
	_test_eof97: cs = 97; goto _test_eof; 
	_test_eof98: cs = 98; goto _test_eof; 
	_test_eof99: cs = 99; goto _test_eof; 
	_test_eof100: cs = 100; goto _test_eof; 
	_test_eof101: cs = 101; goto _test_eof; 
	_test_eof102: cs = 102; goto _test_eof; 
	_test_eof103: cs = 103; goto _test_eof; 
	_test_eof104: cs = 104; goto _test_eof; 
	_test_eof105: cs = 105; goto _test_eof; 
	_test_eof106: cs = 106; goto _test_eof; 
	_test_eof107: cs = 107; goto _test_eof; 
	_test_eof108: cs = 108; goto _test_eof; 
	_test_eof109: cs = 109; goto _test_eof; 
	_test_eof110: cs = 110; goto _test_eof; 
	_test_eof111: cs = 111; goto _test_eof; 
	_test_eof112: cs = 112; goto _test_eof; 
	_test_eof113: cs = 113; goto _test_eof; 
	_test_eof114: cs = 114; goto _test_eof; 
	_test_eof115: cs = 115; goto _test_eof; 
	_test_eof116: cs = 116; goto _test_eof; 
	_test_eof117: cs = 117; goto _test_eof; 
	_test_eof118: cs = 118; goto _test_eof; 
	_test_eof119: cs = 119; goto _test_eof; 
	_test_eof120: cs = 120; goto _test_eof; 
	_test_eof121: cs = 121; goto _test_eof; 
	_test_eof122: cs = 122; goto _test_eof; 
	_test_eof123: cs = 123; goto _test_eof; 
	_test_eof124: cs = 124; goto _test_eof; 
	_test_eof125: cs = 125; goto _test_eof; 
	_test_eof126: cs = 126; goto _test_eof; 
	_test_eof127: cs = 127; goto _test_eof; 
	_test_eof128: cs = 128; goto _test_eof; 
	_test_eof129: cs = 129; goto _test_eof; 
	_test_eof130: cs = 130; goto _test_eof; 
	_test_eof131: cs = 131; goto _test_eof; 
	_test_eof132: cs = 132; goto _test_eof; 
	_test_eof133: cs = 133; goto _test_eof; 
	_test_eof134: cs = 134; goto _test_eof; 
	_test_eof135: cs = 135; goto _test_eof; 
	_test_eof136: cs = 136; goto _test_eof; 
	_test_eof137: cs = 137; goto _test_eof; 
	_test_eof138: cs = 138; goto _test_eof; 
	_test_eof139: cs = 139; goto _test_eof; 
	_test_eof140: cs = 140; goto _test_eof; 
	_test_eof141: cs = 141; goto _test_eof; 
	_test_eof142: cs = 142; goto _test_eof; 
	_test_eof143: cs = 143; goto _test_eof; 
	_test_eof144: cs = 144; goto _test_eof; 
	_test_eof145: cs = 145; goto _test_eof; 
	_test_eof146: cs = 146; goto _test_eof; 
	_test_eof147: cs = 147; goto _test_eof; 
	_test_eof148: cs = 148; goto _test_eof; 
	_test_eof149: cs = 149; goto _test_eof; 
	_test_eof150: cs = 150; goto _test_eof; 
	_test_eof151: cs = 151; goto _test_eof; 
	_test_eof152: cs = 152; goto _test_eof; 
	_test_eof153: cs = 153; goto _test_eof; 
	_test_eof154: cs = 154; goto _test_eof; 
	_test_eof155: cs = 155; goto _test_eof; 
	_test_eof156: cs = 156; goto _test_eof; 
	_test_eof157: cs = 157; goto _test_eof; 
	_test_eof158: cs = 158; goto _test_eof; 
	_test_eof159: cs = 159; goto _test_eof; 
	_test_eof160: cs = 160; goto _test_eof; 
	_test_eof161: cs = 161; goto _test_eof; 
	_test_eof162: cs = 162; goto _test_eof; 
	_test_eof163: cs = 163; goto _test_eof; 
	_test_eof164: cs = 164; goto _test_eof; 
	_test_eof165: cs = 165; goto _test_eof; 
	_test_eof166: cs = 166; goto _test_eof; 
	_test_eof167: cs = 167; goto _test_eof; 
	_test_eof168: cs = 168; goto _test_eof; 
	_test_eof169: cs = 169; goto _test_eof; 
	_test_eof170: cs = 170; goto _test_eof; 
	_test_eof171: cs = 171; goto _test_eof; 
	_test_eof172: cs = 172; goto _test_eof; 
	_test_eof173: cs = 173; goto _test_eof; 
	_test_eof174: cs = 174; goto _test_eof; 
	_test_eof175: cs = 175; goto _test_eof; 
	_test_eof176: cs = 176; goto _test_eof; 
	_test_eof177: cs = 177; goto _test_eof; 
	_test_eof178: cs = 178; goto _test_eof; 
	_test_eof179: cs = 179; goto _test_eof; 
	_test_eof180: cs = 180; goto _test_eof; 
	_test_eof181: cs = 181; goto _test_eof; 
	_test_eof182: cs = 182; goto _test_eof; 
	_test_eof183: cs = 183; goto _test_eof; 
	_test_eof184: cs = 184; goto _test_eof; 
	_test_eof185: cs = 185; goto _test_eof; 
	_test_eof186: cs = 186; goto _test_eof; 
	_test_eof187: cs = 187; goto _test_eof; 
	_test_eof188: cs = 188; goto _test_eof; 
	_test_eof189: cs = 189; goto _test_eof; 
	_test_eof190: cs = 190; goto _test_eof; 
	_test_eof191: cs = 191; goto _test_eof; 
	_test_eof192: cs = 192; goto _test_eof; 
	_test_eof193: cs = 193; goto _test_eof; 
	_test_eof194: cs = 194; goto _test_eof; 
	_test_eof195: cs = 195; goto _test_eof; 
	_test_eof196: cs = 196; goto _test_eof; 

	_test_eof: {}
	_out: {}
	}

#line 307 "src/memcached-grammar.rl"


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
