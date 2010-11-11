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

#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>

#include <say.h>
#include <log_io.h>
#include "log_io_internal.h"

struct remote_state {
	struct recovery_state *r;
	int (*handler) (struct recovery_state *r, struct tbuf *row);
};

static struct tbuf *
row_reader_v11(struct palloc_pool *pool)
{
	const int header_size = sizeof(struct row_v11);
	struct tbuf *m = tbuf_alloc(pool);
	tbuf_ensure(m, header_size);

	if (fiber_read(m->data, header_size) != header_size) {
		say_error("unexpected eof reading row header");
		return NULL;
	}
	tbuf_ensure(m, header_size + row_v11(m)->len);
	m->len = header_size + row_v11(m)->len;

	if (fiber_read(row_v11(m)->data, row_v11(m)->len) != row_v11(m)->len) {
		say_error("unexpected eof reading row body");
		return NULL;
	}

	say_debug("read row bytes:%" PRIu32 " %s", m->len, tbuf_to_hex(m));
	return m;
}

static struct tbuf *
remote_read_row(i64 initial_lsn)
{
	struct tbuf *row;
	bool warning_said = false;
	const int reconnect_delay = 1;
	const char *err = NULL;
	u32 version;

	for (;;) {
		if (fiber->fd < 0) {
			if (fiber_connect(fiber->data) < 0) {
				err = "can't connect to feeder";
				goto err;
			}

			if (fiber_write(&initial_lsn, sizeof(initial_lsn)) != sizeof(initial_lsn)) {
				err = "can't write version";
				goto err;
			}

			if (fiber_read(&version, sizeof(version)) != sizeof(version)) {
				err = "can't read version";
				goto err;
			}

			if (version != default_version) {
				err = "remote version mismatch";
				goto err;
			}

			say_crit("succefully connected to feeder");
			say_crit("starting remote recovery from lsn:%" PRIi64, initial_lsn);
			warning_said = false;
			err = NULL;
		}

		row = row_reader_v11(fiber->pool);
		if (row == NULL) {
			err = "can't read row";
			goto err;
		}

		return row;

	      err:
		if (err != NULL && !warning_said) {
			say_info("%s", err);
			say_info("will retry every %i second", reconnect_delay);
			warning_said = true;
		}
		fiber_sleep(reconnect_delay);
	}
}

static void
pull_from_remote(void *state)
{
	struct remote_state *h = state;
	struct tbuf *row;

	for (;;) {
		row = remote_read_row(confirmed_lsn(h->r) + 1);
		h->r->recovery_lag = ev_now() - row_v11(row)->tm;

		if (h->handler(h->r, row) < 0)
			continue;

		prelease_after(fiber->pool, 128 * 1024);
	}
}

int
default_remote_row_handler(struct recovery_state *r, struct tbuf *row)
{
	struct tbuf *data;
	i64 lsn = row_v11(row)->lsn;

	/* save row data since wal_row_handler may clobber it */
	data = tbuf_alloc(row->pool);
	tbuf_append(data, row_v11(row)->data, row_v11(row)->len);

	if (r->wal_row_handler(r, row) < 0)
		panic("replication failure: can't apply row");

	if (wal_write(r, lsn, data) == false)
		panic("replication failure: can't write row to WAL");

	next_lsn(r, lsn);
	confirm_lsn(r, lsn);

	return 0;
}

struct fiber *
recover_follow_remote(struct recovery_state *r, char *ip_addr, int port,
		      int (*handler) (struct recovery_state *r, struct tbuf *row))
{
	char *name;
	struct fiber *f;
	struct in_addr server;
	struct sockaddr_in *addr;
	struct remote_state *h;

	say_crit("initializing remote hot standby, WAL feeder %s:%i", ip_addr, port);
	name = palloc(eter_pool, 64);
	snprintf(name, 64, "remote_hot_standby/%s:%i", ip_addr, port);

	h = palloc(eter_pool, sizeof(*h));
	h->r = r;
	h->handler = handler;

	f = fiber_create(name, -1, -1, pull_from_remote, h);
	if (f == NULL)
		return NULL;

	if (inet_aton(ip_addr, &server) < 0) {
		say_syserror("inet_aton: %s", ip_addr);
		return NULL;
	}

	addr = palloc(eter_pool, sizeof(*addr));
	memset(addr, 0, sizeof(*addr));
	addr->sin_family = AF_INET;
	memcpy(&addr->sin_addr.s_addr, &server, sizeof(server));
	addr->sin_port = htons(port);
	f->data = addr;
	fiber_call(f);
	return f;
}
