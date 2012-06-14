
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

#include <signal.h>
#include <errno.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_net.h>
#include <connector/c/include/tarantool/tnt_sql.h>

#include "client/tarantool/tc_opt.h"
#include "client/tarantool/tc_admin.h"
#include "client/tarantool/tc.h"
#include "client/tarantool/tc_query.h"
#include "client/tarantool/tc_cli.h"

#define TC_DEFAULT_HISTORY_FILE ".tarantool_history"

extern struct tc tc;

static inline int tc_cli_error(char *e) {
	if (e) {
		printf("%s\n", e);
		free(e);
	}
	return 1;
}

static int tc_cli_reconnect(void) {
	if (tnt_connect(tc.net) == -1) {
		printf("reconnect: %s\n", tnt_strerror(tc.net));
		return 1;
	}
	if (tc_admin_reconnect(&tc.admin) == -1) {
		printf("reconnect: admin console connection failed\n");
		return 1;
	}
	printf("reconnected\n");
	return 0;
}

enum tc_cli_cmd_ret {
	TC_CLI_OK,
	TC_CLI_ERROR,
	TC_CLI_EXIT
};

static enum tc_cli_cmd_ret tc_cli_cmd(char *cmd, size_t size)
{
	int reconnect = 0;
	do {
		if (reconnect) {
			reconnect = tc_cli_reconnect();
			if (reconnect)
				return TC_CLI_ERROR;
		}
		char *e;
		if (tnt_query_is(cmd, size)) {
			if (tc_query(cmd, &e) == 0) {
				if (tc_query_foreach(tc_query_printer, NULL, &e) == -1)
					reconnect = tc_cli_error(e);
			} else {
				reconnect = tc_cli_error(e);
			}
			/* reconnect only for network errors */
			if (reconnect && tnt_error(tc.net) != TNT_ESYSTEM)
				reconnect = 0;
		} else {
			int reply = strcmp(cmd, "exit") &&
				    strcmp(cmd, "quit");
			tc_query_admin_t cb = tc_query_admin_printer;
			if (!reply)
				cb = NULL;
			if (tc_query_admin(cmd, cb, &e) == -1)
				reconnect = tc_cli_error(e);
			if (!reply)
				return TC_CLI_EXIT;
		}
	} while (reconnect);

	return TC_CLI_OK;
}

int tc_cli_cmdv(void)
{
	int i, rc = 0;
	for (i = 0 ; i < tc.opt.cmdc ; i++) {
		enum tc_cli_cmd_ret ret =
			tc_cli_cmd(tc.opt.cmdv[i], strlen(tc.opt.cmdv[i]));
		if (ret == TC_CLI_EXIT)
			break;
		if (ret == TC_CLI_ERROR) {
			rc = 1;
			break;
		}
	}
	return rc;
}

static void tc_cli_init(void) {
	/* ignoring SIGPIPE for reconnection handling */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &sa, NULL) == -1)
		tc_error("signal initialization failed\n");
}

int tc_cli(void)
{
	/* initializing cli */
	tc_cli_init();

	/* loading history file */
	char *home = getenv("HOME");
	char history[1024];
	snprintf(history, sizeof(history), "%s/%s", home,
		 TC_DEFAULT_HISTORY_FILE);
	read_history(history);

	/* setting prompt */
	char prompt[128];
	snprintf(prompt, sizeof(prompt), "%s> ", tc.opt.host);

	/* interactive mode */
	char *cmd;
	while ((cmd = readline(prompt))) {
		if (!cmd[0])
			goto next;
		enum tc_cli_cmd_ret ret =
			tc_cli_cmd(cmd, strlen(cmd));
		if (ret == TC_CLI_EXIT)
			break;
		add_history(cmd);
next:
		free(cmd);
	}

	/* updating history file */
	write_history(history);
	clear_history();
	return 0;
}
