
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

#include <lib/tarantool.h>
#include <locale.h>
#include <unistd.h>

#include <client/tarantool/opt.h>
#include <client/tarantool/main.h>
#include <client/tarantool/pager.h>
#include <client/tarantool/cli.h>
#include <client/tarantool/print.h>
/*#include <client/tarantool/store.h>*/
#include <client/tarantool/query.h>
/*#include <client/tarantool/print_snap.h>*/
/*#include <client/tarantool/print_xlog.h>*/

struct tarantool_client tc;

static void
tc_init(void)
{
	memset(&tc, 0, sizeof(tc));
	setlocale(LC_ALL, "");
	tc.pager_fd = fileno(stdout);
	tc.pager_pid = 0;
	tb_sesinit(&tc.admin);
}

static void
tc_shutdown(void)
{
	tb_sesclose(&tc.admin);
	tc_pager_kill();
}

void tc_error(char *fmt, ...)
{
	char msg[256];
	va_list args;
	tc_shutdown();
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	tc_printf("error: %s\n", msg);
	exit(1);
}

static int
tc_motdof(char *r)
{
	if (strcmp(r, TC_ERRCMD) != 0)
		tc_printf("%s", r);
	return 0;
}

static void
tc_motd(void)
{
	int rc = tc_query("motd()", tc_motdof);
	if (rc == -1)
		tc_error("%s\n", "failed to send admin query");
}

static int
tc_primaryportof(char *r)
{
	if (strcmp(r, TC_ERRCMD) != 0)
		return 0;
	int port = 0;
	sscanf(r, "---\n - %d\n...", &port);
	return port;
}

static int
tc_primaryport()
{
	int rc = tc_query("box.cfg.primary_port", tc_primaryportof);
	if (rc == -1)
		tc_error("%s\n", "failed to send admin query");
	if (rc > 0)
		return rc;
	rc = tc_query("lua box.cfg.primary_port", tc_primaryportof);
	if (rc == -1)
		tc_error("%s\n", "failed to send admin query");
	return rc;
}

static void
tc_connect(void)
{
	tb_sesset(&tc.admin, TB_HOST, tc.opt.host);
	tb_sesset(&tc.admin, TB_PORT, tc.opt.port_admin);
	tb_sesset(&tc.admin, TB_SENDBUF, 0);
	tb_sesset(&tc.admin, TB_READBUF, 0);

	int rc = tb_sesconnect(&tc.admin);
	if (rc == -1)
		tc_error("admin console connection failed");

	if (tc.opt.port == 0)
		tc.opt.port = tc_primaryport();
}

#if 0
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
#endif

int main(int argc, char *argv[], char *envp[])
{
	tc_init();
	enum tc_opt_mode mode =
		tc_opt_init(&tc.opt, argc, argv, envp);

	/*tc_validate();*/

	int rc = 0;
	switch (mode) {
	case TC_OPT_USAGE:
		tc_opt_usage();
		break;
	case TC_OPT_VERSION:
		tc_opt_version();
		break;
#if 0
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
#endif
	case TC_OPT_CMD:
		tc_connect();
		rc = tc_cli_cmdv();
		break;
	case TC_OPT_INTERACTIVE:
		tc_connect();
		tc_motd();
		rc = tc_cli();
		break;
	}

	tc_shutdown();
	return rc;
}
