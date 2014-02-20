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
#include "tarantool.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "log_io.h"
#include "fiber.h"
#include "pickle.h"
#include "coio_buf.h"
#include "recovery.h"
#include "tarantool.h"
#include "iproto_constants.h"
#include "msgpuck/msgpuck.h"
#include "replica.h"

static void
remote_apply_row(struct recovery_state *r, const struct log_row *row);

static const struct log_row *
remote_read_row(struct ev_io *coio, struct iobuf *iobuf)
{
	struct ibuf *in = &iobuf->in;
	ssize_t to_read = sizeof(struct log_row) - ibuf_size(in);

	if (to_read > 0) {
		ibuf_reserve(in, cfg_readahead);
		coio_breadn(coio, in, to_read);
	}

	ssize_t request_len = ((const struct log_row *) in->pos)->len +
			sizeof(struct log_row);
	to_read = request_len - ibuf_size(in);

	if (to_read > 0)
		coio_breadn(coio, in, to_read);

	const struct log_row *row = (const struct log_row *) in->pos;
	in->pos += request_len;
	return row;
}

struct iproto_subscribe_request {
	uint8_t v_len;                         /* length */
	uint8_t m_header;                       /* MP_MAP */
	uint8_t k_code;                         /* IPROTO_CODE */
	uint8_t v_code;                        /* response status */
	uint8_t m_body;                         /* MP_MAP */
	uint8_t k_data;                         /* IPROTO_OFFSET */
	uint8_t m_data;                         /* MP_UINT64 */
	uint64_t lsn;                           /* lsn */
} __attribute__((packed));

static const struct iproto_subscribe_request iproto_subscribe_request = {
	sizeof(struct iproto_subscribe_request) - 1,
	0x81, IPROTO_CODE, IPROTO_SUBSCRIBE,
	0x81, IPROTO_OFFSET, 0xcf, 0
};

int
replica_bootstrap(const char *replication_source)
{
	char ip_addr[32];
	int port;
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));

	int rc = sscanf(replication_source, "%31[^:]:%i",
			ip_addr, &port);

	assert(rc == 2);
	(void)rc;

	addr.sin_family = AF_INET;
	if (inet_aton(ip_addr, (in_addr*)&addr.sin_addr.s_addr) < 0)
		panic_syserror("inet_aton: %s", ip_addr);

	addr.sin_port = htons(port);

	int master = sio_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	FDGuard guard(master);
	sio_connect(master, &addr, sizeof(addr));
	sio_write(master, &iproto_subscribe_request,
		  sizeof(iproto_subscribe_request));

	guard.fd = -1;
	return master;
}

static void
remote_connect(struct ev_io *coio, struct sockaddr_in *remote_addr,
	       int64_t initial_lsn, const char **err)
{
	evio_socket(coio, AF_INET, SOCK_STREAM, IPPROTO_TCP);

	*err = "can't connect to master";
	coio_connect(coio, remote_addr);

	struct iproto_subscribe_request request = iproto_subscribe_request;
	request.lsn = mp_bswap_u64(initial_lsn);
	coio_write(coio, &request, sizeof(request));

	say_crit("successfully connected to master");
	say_crit("starting replication from lsn: %" PRIi64, initial_lsn);
}

static void
pull_from_remote(va_list ap)
{
	struct recovery_state *r = va_arg(ap, struct recovery_state *);
	struct ev_io coio;
	struct iobuf *iobuf = NULL;
	bool warning_said = false;
	const int reconnect_delay = 1;
	ev_loop *loop = loop();

	coio_init(&coio);

	for (;;) {
		const char *err = NULL;
		try {
			fiber_setcancellable(true);
			if (! evio_is_active(&coio)) {
				title("replica", "%s/%s", r->remote->source,
				      "connecting");
				if (iobuf == NULL)
					iobuf = iobuf_new(fiber_name(fiber()));
				remote_connect(&coio, &r->remote->addr,
					       r->confirmed_lsn + 1, &err);
				warning_said = false;
				title("replica", "%s/%s", r->remote->source,
				      "connected");
			}
			err = "can't read row";
			const struct log_row *row = remote_read_row(&coio, iobuf);
			fiber_setcancellable(false);
			err = NULL;

			r->remote->recovery_lag = ev_now(loop) - row->tm;
			r->remote->recovery_last_update_tstamp =
				ev_now(loop);

			remote_apply_row(r, row);

			iobuf_gc(iobuf);
			fiber_gc();
		} catch (FiberCancelException *e) {
			title("replica", "%s/%s", r->remote->source, "failed");
			iobuf_delete(iobuf);
			evio_close(loop, &coio);
			throw;
		} catch (Exception *e) {
			title("replica", "%s/%s", r->remote->source, "failed");
			e->log();
			if (! warning_said) {
				if (err != NULL)
					say_info("%s", err);
				say_info("will retry every %i second", reconnect_delay);
				warning_said = true;
			}
			evio_close(loop, &coio);
		}

		/* Put fiber_sleep() out of catch block.
		 *
		 * This is done to avoid situation, when two or more
		 * fibers yield's inside their try/catch blocks and
		 * throws an exceptions. Seems like exception unwinder
		 * stores some global state while being inside a catch
		 * block.
		 *
		 * This could lead to incorrect exception processing
		 * and crash the server.
		 *
		 * See: https://github.com/tarantool/tarantool/issues/136
		*/
		if (! evio_is_active(&coio))
			fiber_sleep(reconnect_delay);
	}
}

static void
remote_apply_row(struct recovery_state *r, const struct log_row *row)
{
	assert(row->tag == WAL);

	if (r->row_handler(r->row_handler_param, row) < 0)
		panic("replication failure: can't apply row");

	set_lsn(r, row->lsn);
}

void
recovery_follow_remote(struct recovery_state *r, const char *addr)
{
	char name[FIBER_NAME_MAX];
	char ip_addr[32];
	int port;
	int rc;
	struct fiber *f;
	struct in_addr server;

	assert(r->remote == NULL);

	say_crit("initializing the replica, WAL master %s", addr);
	snprintf(name, sizeof(name), "replica/%s", addr);

	try {
		f = fiber_new(name, pull_from_remote);
	} catch (Exception *e) {
		return;
	}

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
	snprintf(remote.source, sizeof(remote.source), "%s", addr);
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
