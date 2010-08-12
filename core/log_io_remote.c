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


static struct tbuf *
row_reader_v04(struct palloc_pool *pool)
{
	const int header_size = offsetof(struct row_v04, data);
	struct tbuf *m = tbuf_alloc(pool);

	if (fiber_read(m->data, header_size) != header_size) {
		say_error("unexpected eof reading row header");
		return NULL;
	}
	m->len = header_size;

	tbuf_ensure(m, header_size + row_v04(m)->len);
	if (fiber_read(row_v04(m)->data, row_v04(m)->len) != row_v04(m)->len) {
		say_error("unexpected eof reading row body");
		return NULL;
	}
	m->len += row_v04(m)->len;

	say_debug("read row bytes:%"PRIu32" %s", m->len, tbuf_to_hex(m));
	return m;
}


static void
pull_from_remote(void *state)
{
	struct recovery_state *r = state;
	struct tbuf *row;
	i64 lsn;
	int rows = 0;
	bool warning_said = false;
	const int reconnect_delay = 1;

	for (;;) {
		if (fiber->fd < 0) {
			if (fiber_connect(fiber->data) < 0) {
				if (!warning_said) {
					say_syserror("can't connect to feeder");
					say_info("will retry every %i second", reconnect_delay);
					warning_said = true;
				}
				fiber_sleep(reconnect_delay);
				continue;
			}
			say_crit("succefully connected to feeder");

			lsn = confirmed_lsn(r) + 1;
			fiber_write(&lsn, sizeof(lsn));

			say_crit("starting remote recovery from lsn:%"PRIi64, lsn);
			warning_said = false;
		}

		row = row_reader_v04(fiber->pool);
		if (row == NULL) {
			fiber_close();
			fiber_sleep(reconnect_delay);
			continue;
		}

		if (r->wal_class.handler(r, row) < 0)
			panic("replication failure: can't apply row");

		if (wal_write(r, r->wal_class.row_lsn(row), row) == false)
			panic("replication failure: can't write row to WAL");

		next_lsn(r, r->wal_class.row_lsn(row));
		confirm_lsn(r, r->wal_class.row_lsn(row));

		if (rows++ % 1000 == 0) {
			prelease(fiber->pool);
			rows = 0;
		}
	}
}


struct fiber *
recover_follow_remote(struct recovery_state *r, char *ip_addr, int port)
{
	char *name;
	struct fiber *f;
	struct in_addr server;
	struct sockaddr_in *addr;

	say_crit("initializing remote hot standby, WAL feeder %s:%i", ip_addr, port);
	name = palloc(eter_pool, 64);
	snprintf(name, 64, "remote_hot_standby/%s:%i", ip_addr, port);
	f = fiber_create(name, -1, -1, pull_from_remote, r);
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
