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
#include "recovery.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "log_io.h"
#include "fiber.h"
#include "pickle.h"

static int
remote_apply_row(struct recovery_state *r, struct tbuf *row);

static struct tbuf *
remote_row_reader_v11()
{
	ssize_t to_read = sizeof(struct header_v11) - fiber->rbuf.size;

	if (to_read > 0 && fiber_bread(&fiber->rbuf, to_read) <= 0)
		goto error;

	ssize_t request_len = header_v11(&fiber->rbuf)->len + sizeof(struct header_v11);
	to_read = request_len - fiber->rbuf.size;

	if (to_read > 0 && fiber_bread(&fiber->rbuf, to_read) <= 0)
		goto error;

	say_debug("read row bytes:%" PRI_SSZ, request_len);
	return tbuf_split(&fiber->rbuf, request_len);
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
pull_from_remote(va_list ap)
{
	struct recovery_state *r = va_arg(ap, struct recovery_state *);
	struct tbuf *row;

	for (;;) {
		fiber_setcancelstate(true);
		row = remote_read_row(&r->remote->addr, r->confirmed_lsn + 1);
		fiber_setcancelstate(false);

		r->remote->recovery_lag = ev_now() - header_v11(row)->tm;
		r->remote->recovery_last_update_tstamp = ev_now();

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
	i64 lsn = header_v11(row)->lsn;
	u16 tag;
	u16 op;

	/* save row data since row_handler() may clobber it */
	data = tbuf_alloc(row->pool);
	tbuf_append(data, row->data + sizeof(struct header_v11), header_v11(row)->len);

	if (r->row_handler(r->row_handler_param, row) < 0)
		panic("replication failure: can't apply row");

	tag = read_u16(data);
	(void) tag;
	(void) read_u64(data); /* drop the cookie */
	op = read_u16(data);

	assert(tag == XLOG);

	if (wal_write(r, lsn, r->remote->cookie, op, data))
		panic("replication failure: can't write row to WAL");

	set_lsn(r, lsn);

	return 0;
}

void
recovery_follow_remote(struct recovery_state *r, const char *addr)
{
	char name[FIBER_NAME_MAXLEN];
	char ip_addr[32];
	int port;
	int rc;
	struct fiber *f;
	struct in_addr server;

	assert(r->remote == NULL);

	say_crit("initializing the replica, WAL master %s", addr);
	snprintf(name, sizeof(name), "replica/%s", addr);

	f = fiber_create(name, -1, pull_from_remote);
	if (f == NULL)
		return;

	rc = sscanf(addr, "%31[^:]:%i", ip_addr, &port);
	assert(rc == 2);
	(void)rc;

	if (inet_aton(ip_addr, &server) < 0) {
		say_syserror("inet_aton: %s", ip_addr);
		return;
	}

	static struct remote remote;
	memset(&remote, 0, sizeof(remote));
	remote.addr.sin_family = AF_INET;
	memcpy(&remote.addr.sin_addr.s_addr, &server, sizeof(server));
	remote.addr.sin_port = htons(port);
	memcpy(&remote.cookie, &remote.addr, MIN(sizeof(remote.cookie), sizeof(remote.addr)));
	remote.reader = f;
	r->remote = &remote;
	fiber_call(f, r);
}

void
recovery_stop_remote(struct recovery_state *r)
{
	say_info("shutting down the replica");
	fiber_cancel(r->remote->reader);
	r->remote = NULL;
}
