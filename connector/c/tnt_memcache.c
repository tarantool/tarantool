
/*
 * Copyright (C) 2011 Mail.RU
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/uio.h>

#include <tnt_error.h>
#include <tnt_mem.h>
#include <tnt_opt.h>
#include <tnt_buf.h>
#include <tnt_main.h>
#include <tnt_io.h>
#include <tnt_memcache_val.h>
#include <tnt_memcache.h>

static int
tnt_memcache_storage(struct tnt *t, char *cmd,
		     int flags, int expire, char *key, char *data, int size,
		     int use_cas, unsigned long long cas)
{
	char buf[256];
	int len;
	if (use_cas)
		len = snprintf(buf, sizeof(buf),
			"%s %s %d %d %d %llu\r\n", cmd, key, flags, expire, size, cas);
	else
		len = snprintf(buf, sizeof(buf),
			"%s %s %d %d %d\r\n", cmd, key, flags, expire, size);

	struct iovec v[3];
	v[0].iov_base = buf;
	v[0].iov_len  = len;
	v[1].iov_base = data;
	v[1].iov_len  = size;
	v[2].iov_base = "\r\n";
	v[2].iov_len  = 2;

	int r = tnt_io_sendv_raw(t, v, 3);
	if (r <= 0) {
		t->error = TNT_ESYSTEM;
		return -1;
	}

	len = tnt_io_recv_raw(t, buf, sizeof(buf));
	if (len <= 0) {
		t->error = TNT_ESYSTEM;
		return -1;
	}

	if (!memcmp(buf, "STORED\r\n", 8))
		return 0;

	/*
	NOT_STORED\r\n
	EXISTS\r\n
	NOT_FOUND\r\n

	ERROR\r\n
	CLIENT_ERROR msg\r\n
	SERVER_ERROR msg\r\n
	*/

	t->error = TNT_EFAIL;
	return -1;
}


int
tnt_memcache_set(struct tnt *t, int flags, int expire, char *key,
		 char *data, int size)
{
	return tnt_memcache_storage(t, "set",
		flags, expire, key, data, size, 0, 0);
}

int
tnt_memcache_add(struct tnt *t, int flags, int expire, char *key,
		 char *data, int size)
{
	return tnt_memcache_storage(t, "add",
		flags, expire, key, data, size, 0, 0);
}

int
tnt_memcache_replace(struct tnt *t, int flags, int expire, char *key,
		     char *data, int size)
{
	return tnt_memcache_storage(t, "replace",
		flags, expire, key, data, size, 0, 0);
}

int
tnt_memcache_append(struct tnt *t, int flags, int expire, char *key,
		    char *data, int size)
{
	return tnt_memcache_storage(t, "append",
		flags, expire, key, data, size, 0, 0);
}

int
tnt_memcache_prepend(struct tnt *t, int flags, int expire, char *key,
		     char *data, int size)
{
	return tnt_memcache_storage(t, "prepand",
		flags, expire, key, data, size, 0, 0);
}

int
tnt_memcache_cas(struct tnt *t, int flags, int expire,
		 unsigned long long cas, char *key,
		 char *data, int size)
{
	return tnt_memcache_storage(t, "cas",
		flags, expire, key, data, size, 1, cas);
}

static int
tnt_memcache_get_tx(struct tnt *t, int cas, int count, char **keys)
{
	int vc = 1 + (2 * count);
	struct iovec *v = tnt_mem_alloc(sizeof(struct iovec) * vc);
	if (v == NULL) {
		t->error = TNT_EMEMORY;
		return -1;
	}

	if (cas) {
		v[0].iov_base = "gets ";
		v[0].iov_len = 5;
	} else {
		v[0].iov_base = "get ";
		v[0].iov_len = 4;
	}

	int i, pos = 1;
	for (i = 0 ; i < count ; i++, pos += 2) {
		v[pos].iov_base = keys[i];
		v[pos].iov_len = strlen(keys[i]);
		if (i + 1 == count) {
			v[pos + 1].iov_base = "\r\n";
			v[pos + 1].iov_len = 2;
		} else {
			v[pos + 1].iov_base = " ";
			v[pos + 1].iov_len = 1;
		}
	}

	int r = tnt_io_sendv_raw(t, v, vc);
	tnt_mem_free(v);
	if (r <= 0) {
		t->error = TNT_ESYSTEM;
		return -1;
	}

	return 0;
}

static int
tnt_memcache_get_rx(struct tnt *t, int cas,
		    int count, struct tnt_memcache_vals *values)
{
	if (tnt_memcache_val_alloc(values, count) == -1) {
		t->error = TNT_EMEMORY;
		return -1;
	}

	/* VALUE <key> <flags> <bytes> [<cas unique>]\r\n
	 * <data block>\r\n
	 * ...
	 * END\r\n
	*/
	int i;
	struct tnt_memcache_val *value = TNT_MEMCACHE_VAL_GET(values, 0);
	for (i = 0 ; i < count ; i++, value++) {
		t->error = tnt_io_recv_expect(t, "VALUE ");
		if (t->error != TNT_EOK)
			goto error;
		/* key */
		int key_len = 0;
		char key[128], ch[1];
		for (;; key_len++) {
			if (key_len > (int)sizeof(key)) {
				t->error = TNT_EBIG;
				goto error;
			}

			t->error = tnt_io_recv_char(t, ch);
			if (t->error != TNT_EOK)
				goto error;

			if (ch[0] == ' ') {
				key[key_len] = 0;
				break;
			}

			key[key_len] = ch[0];
		}

		value->key = tnt_mem_dup(key);
		if (value->key == NULL) {
			t->error = TNT_EMEMORY;
			goto error;
		}

		/* flags */
		value->flags = 0;
		while (1) {
			t->error = tnt_io_recv_char(t, ch);
			if (t->error != TNT_EOK)
				goto error;
			if (!isdigit(ch[0])) {
				if (ch[0] == ' ')
					break;
				t->error = TNT_EPROTO;
				goto error;
			}
			value->flags *= 10;
			value->flags += ch[0] - 48;
		}

		/* bytes */
		value->value_size = 0;
		while (1) {
			t->error = tnt_io_recv_char(t, ch);
			if (t->error != TNT_EOK)
				goto error;

			if (!isdigit(ch[0])) {
				if (ch[0] == ' ' && cas)
					goto cas;
				else
				if (ch[0] == '\r')
					goto lf;

				t->error = TNT_EPROTO;
				goto error;
			}

			value->value_size *= 10;
			value->value_size += ch[0] - 48;
		}
cas:
		value->cas = 0;
		while (1) {
			t->error = tnt_io_recv_char(t, ch);
			if (t->error != TNT_EOK)
				goto error;
			if (!isdigit(ch[0])) {
				if (ch[0] == '\r')
					goto lf;
				else {
					t->error = TNT_EPROTO;
					goto error;
				}
			}

			value->cas *= 10;
			value->cas += ch[0] - 48;
		}
lf:
		t->error = tnt_io_recv_char(t, ch);
		if (t->error != TNT_EOK)
			goto error;

		if (ch[0] != '\n') {
			t->error = TNT_EPROTO;
			goto error;
		}

		/* data */
		value->value = tnt_mem_alloc(value->value_size);
		if (value->value == NULL) {
			t->error = TNT_EMEMORY;
			goto error;
		}

		t->error = tnt_io_recv(t, value->value, value->value_size);
		if (t->error != TNT_EOK)
			goto error;

		t->error = tnt_io_recv_expect(t, "\r\n");
		if (t->error != TNT_EOK)
			goto error;
	}

	t->error = tnt_io_recv_expect(t, "END\r\n");
	if (t->error != TNT_EOK)
		return -1;
	return 0;
error:
	tnt_memcache_val_free(values);
	return -1;
}

int
tnt_memcache_get(struct tnt *t, int cas, int count, char **keys,
		 struct tnt_memcache_vals * values)
{
	if (tnt_memcache_get_tx(t, cas, count, keys) == -1)
		return -1;
	return tnt_memcache_get_rx(t, cas, count, values);
}

int
tnt_memcache_delete(struct tnt *t, char *key, int time)
{
	char buf[256];
	int len = snprintf(buf, sizeof(buf), "delete %s %d\r\n", key, time);

	struct iovec v[1];
	v[0].iov_base = buf;
	v[0].iov_len = len;

	int r = tnt_io_sendv_raw(t, v, 1);
	if (r <= 0) {
		t->error = TNT_ESYSTEM;
		return -1;
	}

	len = tnt_io_recv_raw(t, buf, sizeof(buf));
	if (len <= 0) {
		t->error = TNT_ESYSTEM;
		return -1;
	}

	if (!memcmp(buf, "DELETED\r\n", 9))
		return 0;

	/* NOT_FOUND or ERRORs */
	t->error = TNT_EFAIL;
	return -1;
}

static int
tnt_memcache_unary(struct tnt *t, char *cmd,
		   char *key, unsigned long long val, unsigned long long *valo)
{
	char buf[256];
	int len = snprintf(buf, sizeof(buf), "%s %s %llu\r\n", cmd, key, val);

	struct iovec v[1];
	v[0].iov_base = buf;
	v[0].iov_len  = len;

	int r = tnt_io_sendv_raw(t, v, 1);
	if (r <= 0) {
		t->error = TNT_ESYSTEM;
		return -1;
	}

	char ch[1];
	t->error = tnt_io_recv_char(t, ch);
	if (t->error != TNT_EOK)
		return -1;
	if (!isdigit(ch[0])) {
		/* NOT_FOUND or ERRORs */
		t->error = TNT_EFAIL;
		return -1;
	} 

	*valo = ch[0] - 48;
	while (1) {
		t->error = tnt_io_recv_char(t, ch);
		if (t->error != TNT_EOK)
			return -1;
		if (!isdigit(ch[0])) {
			if (ch[0] == '\r') {
				t->error = tnt_io_recv_char(t, ch);
				if (t->error != TNT_EOK)
					return -1;
				if (ch[0] != '\n') {
					t->error = TNT_EPROTO;
					return -1;
				}
				return 0;
			} 
			t->error = TNT_EPROTO;
			return -1;
		}
		*valo *= 10;
		*valo += ch[0] - 48;
	}

	return -1;
}

int
tnt_memcache_inc(struct tnt *t, char *key,
		 unsigned long long inc, unsigned long long *value)
{
	return tnt_memcache_unary(t, "incr", key, inc, value);
}

int
tnt_memcache_dec(struct tnt *t, char *key,
		 unsigned long long inc, unsigned long long *value)
{
	return tnt_memcache_unary(t, "decr", key, inc, value);
}

int
tnt_memcache_flush_all(struct tnt *t, int time)
{
	char buf[256];
	int len = snprintf(buf, sizeof(buf), "flush_all %d\r\n", time);

	struct iovec v[1];
	v[0].iov_base = buf;
	v[0].iov_len  = len;

	int r = tnt_io_sendv_raw(t, v, 1);
	if (r <= 0) {
		t->error = TNT_ESYSTEM;
		return -1;
	}

	len = tnt_io_recv_raw(t, buf, sizeof(buf));
	if (len <= 0) {
		t->error = TNT_ESYSTEM;
		return -1;
	}
	if (!memcmp(buf, "OK\r\n", 4))
		return 0;

	t->error = TNT_EFAIL;
	return -1;
}
