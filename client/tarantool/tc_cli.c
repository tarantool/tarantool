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
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <wchar.h>

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
#include "client/tarantool/tc_buf.h"

#define TC_DEFAULT_HISTORY_FILE ".tarantool_history"

#define TC_ALLOCATION_ERROR "error: memory allocation failed for %zu bytes\n"
#define TC_REALLOCATION_ERROR "error: memory reallocation failed for %zu bytes\n"

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
	TC_HELP,
	TC_SETOPT,
	TC_SETOPT_DELIM
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
	{ "s", 1, TC_SETOPT},
	{ "setopt", 6, TC_SETOPT},
	{ "delim", 5, TC_SETOPT_DELIM},
	{ "delimiter", 9, TC_SETOPT_DELIM},
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
		" - setopt key=val\n"
		" - (possible pairs: delim=\'str\')\n"
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
	fsync(tc.tee_fd);
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
	char *buf = (char *)malloc(st.st_size);
	if (buf == NULL) {
		tc_printf(TC_ALLOCATION_ERROR,
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
	case TC_SETOPT:
		switch (tnt_lex(&lex, &tk)) {
		case TC_SETOPT_DELIM:
			if (tnt_lex(&lex, &tk) == '=' &&
			    tnt_lex(&lex, &tk) == TNT_TK_STRING) {
				if (!TNT_TK_S(tk)->size) {
					tc.opt.delim = "";
					tc.opt.delim_len = 0;
					goto done;
				}
				char * temp = (char *)malloc(TNT_TK_S(tk)->size);
				if (temp == NULL)
					tc_error(TC_ALLOCATION_ERROR,
						 TNT_TK_S(tk)->size);
				strncpy(temp,
					(const char *)TNT_TK_S(tk)->data,
					TNT_TK_S(tk)->size + 1);
				tc.opt.delim = temp;
				tc.opt.delim_len = strlen(tc.opt.delim);
			} else {
				tc_printf("---\n");
				tc_printf(" - Expected: setopt delim[iter]='string'\n");
				tc_printf("---\n");
			}
			break;
		default:
			tc_printf("---\n");
			tc_printf(" - Unknown option to set\n");
			tc_printf("---\n");
		}
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

static char* tc_cli_readline_pipe() {
	int size = 8192, pos = 0;
	const size_t wcsize = sizeof(wchar_t);
	char *str = (char *)malloc(size);
	if (str == NULL)
		tc_error(TC_ALLOCATION_ERROR, size);
	wchar_t c;
	while ((c = getwchar())) {
		if (size < (pos + wcsize)) {
			size *= 2;
			char *nd = (char *)realloc(str, size);
			if (nd == NULL)
				tc_error(TC_REALLOCATION_ERROR, size);
			str = nd;
		}
		if (c == '\r' || c == '\n' || c == WEOF) {
			char c_t = (c != WEOF ? getchar() : '\n');
			if (c_t != '\r' && c_t != '\n')
				ungetc(c_t, stdin);
			wctomb(str + pos++, 0);
			break;
		}
		else
			pos += wctomb(str + pos, c);
	}
	if (pos == 1 && c == WEOF) {
		free(str);
		return NULL;
	}
	return str;
}



static int check_delim(char* str, size_t len, size_t sep_len) {
	const char *sep = tc.opt.delim;
	len = strip_end_ws(str);
	if (sep_len == 0)
		return 1;
	if (len < sep_len)
		return 0;
	size_t i;
	for (i = len; sep_len > 0; --sep_len, --i)
		if (str[i - 1] != sep[sep_len - 1])
			return 0;
	str[i] = '\0';
	len = strip_end_ws(str);
	return 1;
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
	int prompt_len = snprintf(prompt, sizeof(prompt), "%s> ", tc.opt.host) - 2;
	char prompt_delim[128];
	/* interactive mode */
	char *part_cmd;
	struct tc_buf cmd;
	if (tc_buf_str(&cmd))
		tc_error(TC_REALLOCATION_ERROR,
			 cmd.size);
	while (1) {
		if (isatty(STDIN_FILENO)) {
			snprintf(prompt_delim, sizeof(prompt_delim),
				 "%*s> ", prompt_len, "-");
			part_cmd = readline(!tc_buf_str_isempty(&cmd) ? prompt_delim
							   : prompt);
		} else {
			clearerr(stdin);
			part_cmd = tc_cli_readline_pipe();
		}
		if (!part_cmd)
			break;
		size_t part_cmd_len = strlen(part_cmd);
		int delim_exists = check_delim(part_cmd,
					       part_cmd_len,
					       tc.opt.delim_len);
		if (tc_buf_str_append(&cmd, part_cmd, strlen(part_cmd)))
			tc_error(TC_REALLOCATION_ERROR,
				 cmd.size);
		free(part_cmd);
		if (!delim_exists && !feof(stdin)) {
			if (tc_buf_str_append(&cmd, " ", 1))
				tc_error(TC_REALLOCATION_ERROR,
					 cmd.size);
			continue;
		}
		tc_buf_str_stripws(&cmd);
		if (delim_exists && tc_buf_str_isempty(&cmd))
			goto next;
		tc_print_cmd2tee(cmd.used != 1 ? prompt_delim : prompt,
				 cmd.data, cmd.used - 1);
		enum tc_cli_cmd_ret ret = tc_cli_cmd(cmd.data,
						     cmd.used - 1);
		if (isatty(STDIN_FILENO))
			add_history(cmd.data);
next:
		tc_buf_clear(&cmd);
		if (ret == TC_CLI_EXIT || feof(stdin)) {
			tc_buf_free(&cmd);
			break;
		}
}

	/* updating history file */
	write_history(history);
	clear_history();
	return 0;
}

#undef TC_ALLOCATION_ERROR
#undef TC_REALLOCATION_ERROR
