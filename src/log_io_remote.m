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
#include "log_io.h"
#include "fiber.h"

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
#include <pickle.h>

static int
remote_apply_row(struct recovery_state *r, struct tbuf *row);

static struct tbuf *
remote_row_reader_v11()
{
	ssize_t to_read = sizeof(struct row_v11) - fiber->rbuf->size;

	if (to_read > 0 && fiber_bread(fiber->rbuf, to_read) <= 0)
		goto error;

	ssize_t request_len = row_v11(fiber->rbuf)->len + sizeof(struct row_v11);
	to_read = request_len - fiber->rbuf->size;

	if (to_read > 0 && fiber_bread(fiber->rbuf, to_read) <= 0)
		goto error;

	say_debug("read row bytes:%" PRI_SSZ, request_len);
	return tbuf_split(fiber->rbuf, request_len);
error:
	say_error("unexpected eof reading row header");
	return NULL;
}

static struct tbuf *
remote_read_row(struct sockaddr_in *remote_addr, i64 initial_lsn)
{
	struct tbuf *row;
	bool warning_said = false;
	const int reconnect_delay = 1;
	const char *err = NULL;
	u32 version;

	for (;;) {
		if (fiber->fd < 0) {
			if (fiber_connect(remote_addr) < 0) {
				err = "can't connect to master";
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

			say_crit("successfully connected to master");
			say_crit("starting replication from lsn:%" PRIi64, initial_lsn);

			warning_said = false;
			err = NULL;
		}

		row = remote_row_reader_v11();
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
		fiber_close();
		fiber_sleep(reconnect_delay);
	}
}

static void
pull_from_remote(void *state)
{
	struct recovery_state *r = state;
	struct tbuf *row;

	for (;;) {
		fiber_setcancelstate(true);
		row = remote_read_row(&r->remote_addr, r->confirmed_lsn + 1);
		fiber_setcancelstate(false);

		r->recovery_lag = ev_now() - row_v11(row)->tm;
		r->recovery_last_update_tstamp = ev_now();

		if (remote_apply_row(r, row) < 0) {
			fiber_close();
			continue;
		}

		fiber_gc();
	}
}

static int
remote_apply_row(struct recovery_state *r, struct tbuf *row)
{
	struct tbuf *data;
	i64 lsn = row_v11(row)->lsn;
	u16 tag;
	u16 op;

	/* save row data since wal_row_handler may clobber it */
	data = tbuf_alloc(row->pool);
	tbuf_append(data, row_v11(row)->data, row_v11(row)->len);

	if (r->row_handler(r, row) < 0)
		panic("replication failure: can't apply row");

	tag = read_u16(data);
	(void)read_u64(data); /* drop the cookie */
	op = read_u16(data);

	if (wal_write(r, tag, op, r->cookie, lsn, data))
		panic("replication failure: can't write row to WAL");

	next_lsn(r, lsn);
	confirm_lsn(r, lsn);

	return 0;
}

void
recovery_follow_remote(struct recovery_state *r, const char *remote)
{
	char name[FIBER_NAME_MAXLEN];
	char ip_addr[32];
	int port;
	int rc;
	struct fiber *f;
	struct in_addr server;

	assert(r->remote_recovery == NULL);

	say_crit("initializing the replica, WAL master %s", remote);
	snprintf(name, sizeof(name), "replica/%s", remote);

	f = fiber_create(name, -1, pull_from_remote, r);
	if (f == NULL)
		return;

	rc = sscanf(remote, "%31[^:]:%i", ip_addr, &port);
	assert(rc == 2);
	(void)rc;

	if (inet_aton(ip_addr, &server) < 0) {
		say_syserror("inet_aton: %s", ip_addr);
		return;
	}

	memset(&r->remote_addr, 0, sizeof(r->remote_addr));
	r->remote_addr.sin_family = AF_INET;
	memcpy(&r->remote_addr.sin_addr.s_addr, &server, sizeof(server));
	r->remote_addr.sin_port = htons(port);
	memcpy(&r->cookie, &r->remote_addr, MIN(sizeof(r->cookie), sizeof(r->remote_addr)));
	fiber_call(f);
	r->remote_recovery = f;
}

void
recovery_stop_remote(struct recovery_state *r)
{
	say_info("shutting down the replica");
	fiber_cancel(r->remote_recovery);
	r->remote_recovery = NULL;
	memset(&r->remote_addr, 0, sizeof(r->remote_addr));
}
