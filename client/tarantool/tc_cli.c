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
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_net.h>
#include <connector/c/include/tarantool/tnt_queue.h>
#include <connector/c/include/tarantool/tnt_utf8.h>
#include <connector/c/include/tarantool/tnt_lex.h>
#include <connector/c/include/tarantool/tnt_sql.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_rpl.h>

#include "client/tarantool/tc_opt.h"
#include "client/tarantool/tc_admin.h"
#include "client/tarantool/tc.h"
#include "client/tarantool/tc_query.h"
#include "client/tarantool/tc_cli.h"
#include "client/tarantool/tc_print.h"

#define TC_DEFAULT_HISTORY_FILE ".tarantool_history"

extern struct tc tc;

static inline int tc_cli_error(char *e) {
	if (e) {
		tc_printf("%s\n", e);
		free(e);
	}
	return 1;
}

static int tc_cli_reconnect(void) {
	if (tnt_connect(tc.net) == -1) {
		tc_printf("reconnect: %s\n", tnt_strerror(tc.net));
		return 1;
	}
	if (tc_admin_reconnect(&tc.admin) == -1) {
		tc_printf("reconnect: admin console connection failed\n");
		return 1;
	}
	tc_printf("reconnected\n");
	return 0;
}

enum tc_keywords {
	TC_EXIT = TNT_TK_CUSTOM + 1,
	TC_TEE,
	TC_NOTEE,
	TC_LOADFILE,
	TC_HELP
};

static struct tnt_lex_keyword tc_lex_keywords[] =
{
	{ "e", 1, TC_EXIT },
	{ "ex", 2, TC_EXIT },
	{ "exi", 3, TC_EXIT },
	{ "exit", 4, TC_EXIT },
	{ "q", 1, TC_EXIT },
	{ "qu", 2, TC_EXIT },
	{ "qui", 3, TC_EXIT },
	{ "quit", 4, TC_EXIT },
	{ "help", 4, TC_HELP },
	{ "tee", 3, TC_TEE },
	{ "notee", 5, TC_NOTEE },
	{ "loadfile", 8, TC_LOADFILE },
	{ NULL, 0, TNT_TK_NONE }
};

enum tc_cli_cmd_ret {
	TC_CLI_OK,
	TC_CLI_ERROR,
	TC_CLI_EXIT
};

static void
tc_cmd_usage(void)
{
	char usage[] =
		"---\n"
		"console client commands:\n"
		" - help\n"
		" - tee 'path'\n"
		" - notee\n"
		" - loadfile 'path'\n"
		"...\n";
	tc_printf("%s", usage);
}

static int tc_cli_admin(char *cmd, int exit) {
	char *e = NULL;
	tc_query_admin_t cb = (exit) ? NULL : tc_query_admin_printer;
	if (tc_query_admin(cmd, cb, &e) == -1)
		return tc_cli_error(e);
	return 0;
}

int tc_cmd_tee_close(void)
{
	if (tc.tee_fd == -1)
		return 0;
	fdatasync(tc.tee_fd);
	int rc = close(tc.tee_fd);
	tc.tee_fd = -1;
	return rc;
}

static int tc_cmd_tee_open(char *path)
{
	tc_cmd_tee_close();
	tc.tee_fd = open(path, O_WRONLY|O_CREAT|O_APPEND, 0644);
	if (tc.tee_fd == -1) {
		tc_printf("error: open(): %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int
tc_cmd_dostring(char *buf, size_t size, int *reconnect)
{
	struct tnt_tuple args;
	tnt_tuple_init(&args);
	tnt_tuple_add(&args, buf, size);
	int rc = tnt_call(tc.net, 0, "box.dostring", &args);
	if (rc < 0)
		goto error;
	rc = tnt_flush(tc.net);
	if (rc < 0)
		goto error;
	tnt_tuple_free(&args);
	char *e = NULL;
	if (tc_query_foreach(tc_query_printer, NULL, &e) == -1) {
		*reconnect = tc_cli_error(e);
		return -1;
	}
	return 0;
error:
	tc_printf("error: %s\n", tnt_strerror(tc.net));
	tnt_tuple_free(&args);
	return -1;
}

static int
tc_cmd_loadfile(char *path, int *reconnect)
{
	struct stat st;
	if (stat(path, &st) == -1) {
		tc_printf("error: stat(): %s\n", strerror(errno));
		return -1;
	}
	int fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;
	char *buf = malloc(st.st_size);
	if (buf == NULL) {
		tc_printf("error: memory allocation failed for %zu bytes\n",
			  st.st_size);
		return -1;
	}
	size_t off = 0;
	do {
		ssize_t r = read(fd, buf + off, st.st_size - off);
		if (r == -1) {
			close(fd);
			free(buf);
			tc_printf("error: read(): %s\n", strerror(errno));
			return -1;
		}
		off += r;
	} while (off != st.st_size);
	close(fd);

	int rc = tc_cmd_dostring(buf, st.st_size, reconnect);
	free(buf);
	return rc;
}

static enum tc_cli_cmd_ret
tc_cmd_try(char *cmd, size_t size, int *reconnect)
{
	enum tc_cli_cmd_ret rc = TC_CLI_OK;
	struct tnt_lex lex;
	if (tnt_lex_init(&lex, tc_lex_keywords, (unsigned char*)cmd, size) == -1)
		return TC_CLI_ERROR;
	struct tnt_tk *tk;
	switch (tnt_lex(&lex, &tk)) {
	case TC_EXIT:
		rc = TC_CLI_EXIT;
		break;
	case TC_HELP:
		tc_cmd_usage();
		break;
	case TC_TEE:
		if (tnt_lex(&lex, &tk) != TNT_TK_STRING) {
			rc = TC_CLI_ERROR;
			goto done;
		}
		if (tc_cmd_tee_open((char*)TNT_TK_S(tk)->data) == -1)
			rc = TC_CLI_ERROR;
		goto done;
	case TC_NOTEE:
		tc_cmd_tee_close();
		goto done;
	case TC_LOADFILE:
		if (tnt_lex(&lex, &tk) != TNT_TK_STRING) {
			rc = TC_CLI_ERROR;
			goto done;
		}
		if (tc_cmd_loadfile((char*)TNT_TK_S(tk)->data, reconnect) == -1)
			rc = TC_CLI_ERROR;
		goto done;
	}
	*reconnect = tc_cli_admin(cmd, rc == TC_CLI_EXIT);
	if (*reconnect)
		return TC_CLI_ERROR;
done:
	tnt_lex_free(&lex);
	return rc;
}

static enum tc_cli_cmd_ret tc_cli_cmd(char *cmd, size_t size)
{
	int reconnect = 0;
	do {
		if (reconnect) {
			reconnect = tc_cli_reconnect();
			if (reconnect)
				return TC_CLI_ERROR;
		}
		char *e = NULL;
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
			enum tc_cli_cmd_ret rc = tc_cmd_try(cmd, size, &reconnect);
			if (reconnect)
				continue;
			if (rc == TC_CLI_EXIT || rc == TC_CLI_ERROR)
				return rc;
		}
	} while (reconnect);

	return TC_CLI_OK;
}

int tc_cli_cmdv(void)
{
	int i, rc = 0;
	for (i = 0 ; i < tc.opt.cmdc ; i++) {
		char *cmd = tc.opt.cmdv[i];
		int cmd_len = strlen(tc.opt.cmdv[i]);
		tc_print_cmd2tee(NULL, cmd, cmd_len);
		enum tc_cli_cmd_ret ret = tc_cli_cmd(cmd, cmd_len);
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
		int cmd_len = strlen(cmd);
		tc_print_cmd2tee(prompt, cmd, cmd_len);
		enum tc_cli_cmd_ret ret = tc_cli_cmd(cmd, cmd_len);
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
