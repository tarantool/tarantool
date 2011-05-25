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

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <fiber.h>
#include <util.h>
#include "cfg/tarantool_feeder_cfg.h"
#include "palloc.h"
#include "log_io.h"
#include "say.h"
#include "tarantool.h"

const char *mod_name = "Feeder";
static char *custom_proc_title;

static int
send_row(struct recovery_state *r __attribute__((unused)), struct tbuf *t)
{
	u8 *data = t->data;
	ssize_t bytes, len = t->len;
	while (len > 0) {
		bytes = write(fiber->fd, data, len);
		if (bytes < 0) {
			say_syserror("write");
			exit(EXIT_SUCCESS);
		}
		len -= bytes;
		data += bytes;
	}

	say_debug("send row: %" PRIu32 " bytes %s", t->len, tbuf_to_hex(t));

	return 0;
}

static void
recover_feed_slave(int sock)
{
	struct recovery_state *log_io;
	struct tbuf *ver;
	i64 lsn;
	ssize_t r;

	fiber->has_peer = true;
	fiber->fd = sock;
	fiber_set_name(fiber, "feeder");
	set_proc_title("feeder:client_handler%s %s", custom_proc_title, fiber_peer_name(fiber));

	ev_default_loop(0);

	r = read(fiber->fd, &lsn, sizeof(lsn));
	if (r != sizeof(lsn)) {
		if (r < 0)
			say_syserror("read");
		exit(EXIT_SUCCESS);
	}

	ver = tbuf_alloc(fiber->pool);
	tbuf_append(ver, &default_version, sizeof(default_version));
	send_row(NULL, ver);

	log_io = recover_init(NULL, cfg.wal_feeder_dir,
			      NULL, send_row, INT32_MAX, 0, 64, RECOVER_READONLY, false);

	recover(log_io, lsn);
	recover_follow(log_io, 0.1);
	ev_loop(0);
}

i32
mod_check_config(struct tarantool_cfg *conf __attribute__((unused)))
{
	return 0;
}

i32
mod_reload_config(struct tarantool_cfg *old_conf __attribute__((unused)),
		  struct tarantool_cfg *new_conf __attribute__((unused)))
{
	return 0;
}

void
mod_init(void)
{
	int server, client;
	struct sockaddr_in server_addr;
	int one = 1;

	if (cfg.wal_feeder_bind_port == 0)
		panic("can't start feeder without wal_feeder_bind_port");

	if (cfg.wal_feeder_dir == NULL)
		panic("can't start feeder without wal_feeder_dir");

	if (cfg.custom_proc_title == NULL)
		custom_proc_title = "";
	else {
		custom_proc_title = palloc(eter_pool, strlen(cfg.custom_proc_title) + 2);
		strcat(custom_proc_title, "@");
		strcat(custom_proc_title, cfg.custom_proc_title);
	}

	set_proc_title("feeder:acceptor%s %s:%i",
		       custom_proc_title,
		       cfg.wal_feeder_bind_ipaddr == NULL ? "ANY" : cfg.wal_feeder_bind_ipaddr,
		       cfg.wal_feeder_bind_port);

	server = socket(AF_INET, SOCK_STREAM, 0);
	if (server < 0) {
		say_syserror("socket");
		goto exit;
	}

	memset(&server_addr, 0, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	if (cfg.wal_feeder_bind_ipaddr == NULL) {
		server_addr.sin_addr.s_addr = INADDR_ANY;
	} else {
		server_addr.sin_addr.s_addr = inet_addr(cfg.wal_feeder_bind_ipaddr);
		if (server_addr.sin_addr.s_addr == INADDR_NONE)
			panic("inet_addr: %s'", cfg.wal_feeder_bind_ipaddr);
	}
	server_addr.sin_port = htons(cfg.wal_feeder_bind_port);

	if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
		say_syserror("setsockopt");
		goto exit;
	}

	if (bind(server, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		say_syserror("bind");
		goto exit;
	}

	listen(server, 5);

	for (;;) {
		pid_t child;
		client = accept(server, NULL, NULL);
		if (client < 0) {
			say_syserror("accept");
			continue;
		}
		child = fork();
		if (child < 0) {
			say_syserror("fork");
			continue;
		}
		if (child == 0)
			recover_feed_slave(client);
		else
			close(client);
	}
      exit:
	exit(EXIT_FAILURE);
}

void
mod_exec(char *str __attribute__((unused)), int len __attribute__((unused)),
	 struct tbuf *out)
{
	tbuf_printf(out, "Unimplemented");
}
