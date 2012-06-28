
/*
 * Copyright (C) 2012 Mail.RU
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
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_net.h>
#include <connector/c/include/tarantool/tnt_sql.h>

#include "client/tarantool/tc_opt.h"
#include "client/tarantool/tc_admin.h"
#include "client/tarantool/tc.h"
#include "client/tarantool/tc_cli.h"
#include "client/tarantool/tc_wal.h"

struct tc tc;

static void tc_init(void) {
	memset(&tc, 0, sizeof(tc));
}

static void tc_free(void) {
	if (tc.net) {
		tnt_stream_free(tc.net);
	}
	tc_admin_close(&tc.admin);
}

void tc_error(char *fmt, ...) {
	char msg[256];
	va_list args;
	tc_free();
	/* - - - - */
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	printf("error: %s\n", msg);
	exit(1);
}

static void tc_connect(void)
{
	/* allocating stream */
	tc.net = tnt_net(NULL);
	if (tc.net == NULL)
		tc_error("stream allocation error");
	/* initializing network stream */
	tnt_set(tc.net, TNT_OPT_HOSTNAME, tc.opt.host);
	tnt_set(tc.net, TNT_OPT_PORT, tc.opt.port);
	tnt_set(tc.net, TNT_OPT_SEND_BUF, 0);
	tnt_set(tc.net, TNT_OPT_RECV_BUF, 0);
	if (tnt_init(tc.net) == -1)
		tc_error("%s", tnt_strerror(tc.net));
	/* connecting to server */
	if (tnt_connect(tc.net) == -1)
		tc_error("%s", tnt_strerror(tc.net));
}

static void tc_connect_admin(void)
{
	if (tc_admin_connect(&tc.admin,
			     tc.opt.host,
			     tc.opt.port_admin) == -1)
		tc_error("admin console connection failed");
}

int main(int argc, char *argv[])
{
	tc_init();

	int rc = 0;
	enum tc_opt_mode mode = tc_opt_init(&tc.opt, argc, argv);
	switch (mode) {
	case TC_OPT_USAGE:
		tc_opt_usage();
		break;
	case TC_OPT_VERSION:
		tc_opt_version();
		break;
	case TC_OPT_RPL:
		tc_connect();
		rc = tc_wal_remote();
		break;
	case TC_OPT_WAL_CAT:
		rc = tc_wal_cat();
		break;
	case TC_OPT_WAL_PLAY:
		tc_connect();
		rc = tc_wal_play();
		break;
	case TC_OPT_CMD:
		tc_connect();
		tc_connect_admin();
		rc = tc_cli_cmdv();
		break;
	case TC_OPT_INTERACTIVE:
		tc_connect();
		tc_connect_admin();
		rc = tc_cli();
		break;
	}

	tc_free();
	return rc;
}
