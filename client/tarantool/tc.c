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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <locale.h>

#include <unistd.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_net.h>
#include <connector/c/include/tarantool/tnt_sql.h>
#include <connector/c/include/tarantool/tnt_xlog.h>

#include "client/tarantool/tc_opt.h"
#include "client/tarantool/tc_admin.h"
#include "client/tarantool/tc.h"
#include "client/tarantool/tc_cli.h"
#include "client/tarantool/tc_print.h"
#include "client/tarantool/tc_store.h"
#include "client/tarantool/tc_query.h"
#include "client/tarantool/tc_print_snap.h"
#include "client/tarantool/tc_print_xlog.h"

#define TC_DEFAULT_PORT 33013
#define TC_DEFAULT_PORT 33013
#define TC_ERR_CMD "---\nunknown command. try typing help.\n...\n"

struct tc tc;

static void tc_init(void) {
	memset(&tc, 0, sizeof(tc));
	tc.tee_fd = -1;
	setlocale(LC_ALL, "");
}

static void tc_free(void) {
	if (tc.net) {
		tnt_stream_free(tc.net);
	}
	tc_admin_close(&tc.admin);
	tc_cmd_tee_close();
}

void tc_error(char *fmt, ...) {
	char msg[256];
	va_list args;
	tc_free();
	/* - - - - */
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	tc_printf("error: %s\n", msg);
	exit(1);
}

static void tc_connect(void)
{
	if (tc.opt.port == 0)
		tc.opt.port = TC_DEFAULT_PORT;
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

static char *send_cmd(char *cmd)
{
	size_t size = 0;
	char *reply = NULL;
	if (tc_admin_query(&tc.admin, cmd) == -1)
		tc_error("cannot send query");
	if (tc_admin_reply(&tc.admin, &reply, &size) == -1)
		tc_error("cannot recv query");
	if (strncmp(reply, TC_ERR_CMD, size) == 0) {
		free(reply);
		return NULL;
	}
	return reply;
}

static int get_primary_port()
{
	int port = 0;
	char *reply = send_cmd("box.cfg.primary_port");
	if (reply == NULL)
		reply = send_cmd("lua box.cfg.primary_port");
	if (reply != NULL) {
		sscanf(reply, "---\n - %d\n...", &port);
		free(reply);
	}
	return port;
}

static void tc_connect_admin(void)
{

	if (tc_admin_connect(&tc.admin,
			     tc.opt.host,
			     tc.opt.port_admin) == -1)
		tc_error("admin console connection failed");
	if (tc.opt.port == 0)
		tc.opt.port = get_primary_port();
}

static void tc_validate(void)
{
	tc.opt.xlog_printer = tc_print_getxlogcb(tc.opt.format);
	tc.opt.snap_printer = tc_print_getsnapcb(tc.opt.format);
	if (tc.opt.xlog_printer == NULL)
		tc_error("unsupported output xlog format '%s'",
				tc.opt.format);
	if (tc.opt.snap_printer == NULL)
		tc_error("unsupported output snap format '%s'",
				tc.opt.format);
	if (tc.opt.format && strcmp(tc.opt.format, "raw") == 0)
		tc.opt.raw = 1;
}

int main(int argc, char *argv[])
{
	tc_init();

	int rc = 0;
	enum tc_opt_mode mode = tc_opt_init(&tc.opt, argc, argv);
	tc_validate();
	switch (mode) {
	case TC_OPT_USAGE:
		tc_opt_usage();
		break;
	case TC_OPT_VERSION:
		tc_opt_version();
		break;
	case TC_OPT_RPL:
		tc_connect();
		rc = tc_store_remote();
		break;
	case TC_OPT_WAL_CAT:
		rc = tc_store_cat();
		break;
	case TC_OPT_WAL_PLAY:
		tc_connect();
		rc = tc_store_play();
		break;
	case TC_OPT_CMD:
		tc_connect_admin();
		tc_connect();
		rc = tc_cli_cmdv();
		break;
	case TC_OPT_INTERACTIVE:
		tc_connect_admin();
		tc_connect();
		tc_cli_motd();
		rc = tc_cli();
		break;
	}

	tc_free();
	return rc;
}
