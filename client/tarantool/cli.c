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
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <wchar.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <client/tarantool/opt.h>
#include <client/tarantool/main.h>
#include <client/tarantool/pager.h>
#include <client/tarantool/query.h>
#include <client/tarantool/cli.h>
#include <client/tarantool/print.h>
#include <client/tarantool/buf.h>

extern struct tarantool_client tc;

static inline int tc_clierror(char *e) {
	if (e)
		tc_printf("%s\n", e);
	return 1;
}

static int tc_clireconnect(void)
{
	tb_sesclose(&tc.console);
	int rc = tb_sesconnect(&tc.console);
	if (rc == -1) {
		tc_printf("reconnect: admin console connection failed\n");
		return 1;
	}
	tc_printf("reconnected\n");
	return 0;
}

static int
tc_cliquery(char *cmd, int exit)
{
	tc_query_t cb = (exit) ? NULL : tc_printer;
	if (!exit)
		tc_pager_start();
	if (tc_query(cmd, cb) == -1)
		return tc_clierror("failed to send admin query");
	if (!exit)
		tc_pager_stop();
	return 0;
}

enum tc_keywords {
	TC_EXIT = TB_TCUSTOM + 1,
	TC_LOADFILE,
	TC_HELP,
	TC_SETOPT,
	TC_SETOPT_DELIM,
	TC_SETOPT_PAGER
};

static struct tbkeyword tc_keywords[] =
{
	{ "exit",      4, TC_EXIT         },
	{ "quit",      4, TC_EXIT         },
	{ "help",      4, TC_HELP         },
	{ "loadfile",  8, TC_LOADFILE     },
	{ "setopt",    6, TC_SETOPT       },
	{ "delimiter", 9, TC_SETOPT_DELIM },
	{ "pager",     5, TC_SETOPT_PAGER },
	{ NULL,        0, TB_TNONE        }
};

enum tc_cliret {
	TC_CLI_OK,
	TC_CLI_ERROR,
	TC_CLI_EXIT
};

static void
tc_cmdusage(void)
{
	char usage[] =
		"---\n"
		" - console client commands\n"
		"   - help\n"
		"   - loadfile 'path'\n"
		"   - setopt key=val\n"
		"   - - delimiter = \'string\'\n"
		"     - pager = \'command\'\n"
		"...\n";
	tc_printf("%s", usage);
}

#if 0
static int
tc_cmddostring(char *buf, size_t size, int *reconnect)
{
#if 0
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
	tc_pager_start();
	if (tc_query_foreach(tc_query_printer, NULL, &e) == -1) {
		*reconnect = tc_cl_error(e);
		return -1;
	}
	tc_pager_stop();
	return 0;
error:
	tc_printf("error: %s\n", tnt_strerror(tc.net));
	tnt_tuple_free(&args);
	return -1;
#endif
	(void)buf;
	(void)size;
	(void)reconnect;
	return 0;
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
#endif

static void
tc_strip(char *cmd)
{
	int len = strlen(cmd);
	int offset = 0;
	for (int i = 0; i < len - 1; ++i)
		if (cmd[i] == '\\' && cmd[i + 1] == 'n')
			cmd[i++ - offset++] = '\n';
		else if (offset != 0)
			cmd[i - offset] = cmd[i];
	cmd[len - offset] = '\0';
}

static void
tc_setopt(struct tblex *lex)
{
	struct tbtoken *tk;
	switch (tb_lex(lex, &tk)) {
	case TC_SETOPT_DELIM:
		if (tb_lex(lex, &tk) != '=')
			tb_lexpush(lex, tk);
		if (tb_lex(lex, &tk) != TB_TSTRING) {
			tc_printf("---\n");
			tc_printf(" - Expected delimiter='string'\n");
			tc_printf("---\n");
			break;
		}
		if (! tk->v.s.size) {
			if (tc.opt.delim)
				free(tc.opt.delim);
			tc.opt.delim = NULL;
			tc.opt.delim_len = 0;
			break;
		}
		char *temp = tc_malloc(tk->v.s.size + 1);
		strncpy(temp, (const char*)tk->v.s.data, tk->v.s.size + 1);
		temp[tk->v.s.size] = '\0';
		tc_strip(temp);
		tc.opt.delim = temp;
		tc.opt.delim_len = strlen(tc.opt.delim);
		break;
	case TC_SETOPT_PAGER:
		if (tb_lex(lex, &tk) == '=' &&
			tb_lex(lex, &tk) == TB_TSTRING) {
			if (! tk->v.s.size) {
				if (tc.opt.pager)
					free(tc.opt.pager);
				tc.opt.pager = NULL;
				break;
			}
			char *temp = (char *)malloc(tk->v.s.size + 1);
			if (temp == NULL)
				tc_oom();
			strncpy(temp, (const char *)tk->v.s.data, tk->v.s.size + 1);
			temp[tk->v.s.size] = '\0';
			tc.opt.pager = temp;
		} else {
			tc_printf("---\n");
			tc_printf(" - Expected pager='command'\n");
			tc_printf("---\n");
		}
		break;
	default:
		tc_printf("---\n");
		tc_printf(" - Unknown option to set\n");
		tc_printf("---\n");
		break;
	}
}

static enum tc_cliret
tc_cmdtry(char *cmd, size_t size, int *reconnect)
{
	enum tc_cliret rc = TC_CLI_OK;
	struct tblex lex;
	if (tb_lexinit(&lex, tc_keywords, cmd, size) == -1)
		return TC_CLI_ERROR;
	struct tbtoken *tk;
	switch (tb_lex(&lex, &tk)) {
	case TC_EXIT:
		rc = TC_CLI_EXIT;
		break;
	case TC_HELP:
		tc_cmdusage();
		cmd = "help()";
		break;
	case TC_SETOPT:
		tc_setopt(&lex);
		goto done;
#if 0
	case TC_LOADFILE:
		if (tnt_lex(&lex, &tk) != TNT_TK_STRING) {
			rc = TC_CLI_ERROR;
			goto done;
		}
		if (tc_cmd_loadfile((char*)TNT_TK_S(tk)->data, reconnect) == -1)
			rc = TC_CLI_ERROR;
		goto done;
#endif
	}
	*reconnect = tc_cliquery(cmd, rc == TC_CLI_EXIT);
	if (*reconnect)
		rc = TC_CLI_ERROR;
done:
	tb_lexfree(&lex);
	return rc;
}

static enum tc_cliret
tc_clicmd(char *cmd, size_t size)
{
	int reconnect = 0;
	do {
		if (reconnect) {
			reconnect = tc_clireconnect();
			if (reconnect)
				return TC_CLI_ERROR;
		}
		enum tc_cliret rc = tc_cmdtry(cmd, size, &reconnect);
		if (reconnect)
			continue;
		if (rc == TC_CLI_EXIT || rc == TC_CLI_ERROR)
			return rc;
	} while (reconnect);

	return TC_CLI_OK;
}

int tc_clicmdv(void)
{
	int i, rc = 0;
	for (i = 0 ; i < tc.opt.cmdc ; i++) {
		char *cmd = tc.opt.cmdv[i];
		int len = strlen(tc.opt.cmdv[i]);
		enum tc_cliret ret = tc_clicmd(cmd, len);
		if (ret == TC_CLI_EXIT)
			break;
		if (ret == TC_CLI_ERROR) {
			rc = 1;
			break;
		}
	}
	return rc;
}

static void
tc_cliinit(void)
{
	/* ignore SIGPIPE for reconnection handling */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &sa, NULL) == -1)
		tc_error("signal initialization failed\n");
}

static char*
tc_clipipe(void)
{
	int size = 8192, pos = 0;
	const size_t wcsize = sizeof(wchar_t);
	char *str = malloc(size);
	if (str == NULL)
		tc_oom();
	wchar_t c;
	while ((c = getwchar())) {
		if (size < (pos + wcsize)) {
			size *= 2;
			char *nd = (char *)realloc(str, size);
			if (nd == NULL)
				tc_oom();
			str = nd;
		}
		if (c == '\r' || c == '\n' || c == WEOF) {
			char c_t = (c != WEOF ? getchar() : '\n');
			if (c_t != '\r' && c_t != '\n')
				ungetc(c_t, stdin);
			wctomb(str + pos++, 0);
			break;
		} else {
			pos += wctomb(str + pos, c);
		}
	}
	if (pos == 1 && c == WEOF) {
		free(str);
		return NULL;
	}
	return str;
}

static int
tc_hasdelim(char* str, size_t len, size_t lensep)
{
	const char *sep = tc.opt.delim;
	if (lensep == 0)
		return 1;
	if (len < lensep)
		return 0;
	size_t i;
	for (i = len; lensep > 0; --lensep, --i)
		if (str[i - 1] != sep[lensep - 1])
			return 0;
	return 1;
}

int tc_cli(void)
{
	/* initialize cli */
	tc_cliinit();

	/* load history file */
	char *home = getenv("HOME");
	char history[1024];
	snprintf(history, sizeof(history), "%s/%s", home,
		 TC_DEFAULT_HISTORY_FILE);
	read_history(history);

	/* set prompt */
	char prompt[128];
	int prompt_len =
		snprintf(prompt, sizeof(prompt), "%s> ", tc.opt.host) - 2;
	char prompt_delim[128];

	/* interactive mode */
	char *part_cmd;
	struct tc_buf cmd;
	if (tc_buf_str(&cmd))
		tc_oom();

	while (1) {
		if (isatty(STDIN_FILENO)) {
			snprintf(prompt_delim, sizeof(prompt_delim),
			         "%*s> ", prompt_len, "-");
			part_cmd = readline(!tc_buf_str_isempty(&cmd) ?
			                    prompt_delim : prompt);
		} else {
			clearerr(stdin);
			part_cmd = tc_clipipe();
		}
		if (!part_cmd)
			break;
		if (tc_buf_str_append(&cmd, part_cmd, strlen(part_cmd)))
			tc_oom();

		int delim_exists =
			tc_hasdelim(cmd.data, cmd.used - 1,
			            tc.opt.delim_len);

		if (tc_buf_str_append(&cmd, "\n", 1))
			tc_oom();
		free(part_cmd);
		if (!delim_exists && !feof(stdin))
			continue;
		tc_buf_str_delete(&cmd, 1);
		if (isatty(STDIN_FILENO))
			add_history(cmd.data);
		tc_buf_cmdfy(&cmd, tc.opt.delim_len);
		if (delim_exists && tc_buf_str_isempty(&cmd))
			goto next;

		enum tc_cliret ret = tc_clicmd(cmd.data, cmd.used - 1);
next:
		tc_buf_clear(&cmd);
		if (ret == TC_CLI_EXIT || feof(stdin)) {
			tc_buf_free(&cmd);
			break;
		}
	}

	/* update history file */
	write_history(history);
	clear_history();
	return 0;
}
