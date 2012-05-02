
/*
 * Copyright (C) 2011 Mail.RU
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
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include <signal.h>
#include <errno.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <errcode.h>
#include <third_party/gopt/gopt.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_sql.h>
#include <connector/c/include/tarantool/tnt_net.h>

#include <client/tarantool/tnt_admin.h>

#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT 33013
#define DEFAULT_PORT_ADMIN 33015
#define HISTORY_FILE ".tarantool_history"

static int query_reply_handle(struct tnt_stream *t, struct tnt_reply *r) {
	switch (r->op) {
	case TNT_OP_PING:
		printf("Ping ");
		break;
	case TNT_OP_INSERT:
		printf("Insert ");
		break;
	case TNT_OP_DELETE:
		printf("Delete ");
		break;
	case TNT_OP_UPDATE:
		printf("Update ");
		break;
	case TNT_OP_SELECT:
		printf("Select ");
		break;
	case TNT_OP_CALL:
		printf("Call ");
		break;
	default:
		printf("Unknown ");
		break;
	}
	if (tnt_error(t) != TNT_EOK) {
		printf("ERROR, %s\n", tnt_strerror(t));
		return -1;
	} else if (r->code != 0) {
		printf("ERROR, %s (%s)\n",
		       ((r->error) ? r->error : ""), tnt_errcode_str(r->code >> 8));
		return -1;
	}
	printf("OK, %d rows affected\n", r->count);
	struct tnt_iter it;
	tnt_iter_list(&it, TNT_REPLY_LIST(r));
	while (tnt_next(&it)) {
		printf("[");
		struct tnt_tuple *tu = TNT_ILIST_TUPLE(&it);
		struct tnt_iter ifl;
		tnt_iter(&ifl, tu);
		while (tnt_next(&ifl)) {
			if (TNT_IFIELD_IDX(&ifl) != 0)
				printf(", ");
			char *data = TNT_IFIELD_DATA(&ifl);
			uint32_t size = TNT_IFIELD_SIZE(&ifl);
			if (!isprint(data[0]) && (size == 4 || size == 8)) {
				if (size == 4) {
					uint32_t i = *((uint32_t*)data);
					printf("%"PRIu32, i);
				} else {
					uint64_t i = *((uint64_t*)data);
					printf("%"PRIu64, i);
				}
			} else {
				printf("'%-.*s'", size, data);
			}
		}
		if (ifl.status == TNT_ITER_FAIL)
			printf("<parsing error>");
		printf("]\n");
	}
	return 0;
}

static int
query_reply(struct tnt_stream *t)
{
	int rc = -1;
	struct tnt_iter i;
	tnt_iter_stream(&i, t);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_ISTREAM_REPLY(&i);
		if (query_reply_handle(t, r) == -1)
			goto error;

	}
	rc = (i.status == TNT_ITER_FAIL) ? -1 : 0;
error:
	tnt_iter_free(&i);
	return rc;
}

static int
query(struct tnt_stream *t, char *q)
{
	char *e = NULL;
	int rc = tnt_query(t, q, strlen(q), &e);
	if (rc == -1) {
		if (e) {
			printf("error: %s", e);
			free(e);
		}
		return -1;
	}
	rc = tnt_flush(t);
	if (rc < 0) {
		printf("error: %s\n", tnt_strerror(t));
		return -1;
	}
	if (query_reply(t) == -1)
		return -1;
	return 0;
}

static int
query_admin(struct tnt_admin *a, char *q, int reply)
{
	if (tnt_admin_query(a, q) == -1) {
		printf("error: failed to send admin query\n");
		return -1;
	}
	if (!reply)
		return 0;
	char *rep = NULL;
	size_t reps = 0;
	if (tnt_admin_reply(a, &rep, &reps) == -1) {
		printf("error: failed to recv admin reply\n");
		return -1;
	}
	if (rep) {
		printf("%s", rep);
		free(rep);
	}
	return 0;
}

static int
run_cmdline(struct tnt_stream *t, struct tnt_admin *a, int argc, char **argv) 
{
	int i, rc = 0;
	for (i = 1 ; i < argc ; i++) {
		if (tnt_query_is(argv[i], strlen(argv[i]))) {
			if (query(t, argv[i]) == -1)
				rc = 1;
		} else {
			int reply = strcmp(argv[i], "exit") && strcmp(argv[i], "quit");
			if (query_admin(a, argv[i], reply) == -1)
				rc = 1;
			if (!reply)
				break;
		}
	}
	return rc;
}

static int reconnect_do(struct tnt_stream *t, struct tnt_admin *a) {
	if (tnt_connect(t) == -1) {
		printf("reconnect: %s\n", tnt_strerror(t));
		return 0;
	}
	if (tnt_admin_reconnect(a) == -1) {
		printf("reconnect: admin console connection failed\n");
		return 0;
	}
	printf("reconnected\n");
	return 1;
}

static int
run_interactive(struct tnt_stream *t, struct tnt_admin *a, char *host) 
{
	/* ignoring SIGPIPE */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &sa, NULL) == -1) {
		printf("signal initialization failed\n");
		return 1;
	}

	char *home = getenv("HOME");
	char history[1024];
	snprintf(history, sizeof(history), "%s/%s", home, HISTORY_FILE);
	read_history(history);

	char prompt[128];
	snprintf(prompt, sizeof(prompt), "%s> ", host);

	/* interactive mode */
	int reconnect = 0;
	char *cmd;
	while ((cmd = readline(prompt))) {
		if (!cmd[0])
			goto next;
		if (reconnect) {
reconnect: 		if (reconnect_do(t, a))
				reconnect = 0;
			else 
				goto next;
		}
		if (tnt_query_is(cmd, strlen(cmd))) {
			if (query(t, cmd) == -1) {
				/* broken pipe or recv() == 0 */
				int e = tnt_errno(t) == EPIPE || tnt_errno(t) == 0;
				if (tnt_error(t) == TNT_ESYSTEM && e)
					reconnect = 1;
			}
		} else {
			int reply = strcmp(cmd, "exit") && strcmp(cmd, "quit");
			if (query_admin(a, cmd, reply) == -1)
				reconnect = 1;
			if (!reply) {
				free(cmd);
				break;
			}
		}
		add_history(cmd);
		if (reconnect)
			goto reconnect;
next:
		free(cmd);
	}

	write_history(history);
	clear_history();
	return 0;
}

int
main(int argc, char *argv[])
{
	const void *opt_def =
		gopt_start(gopt_option('a', GOPT_ARG, gopt_shorts('a'),
				       gopt_longs("host"), " <host>", "server address"),
			   gopt_option('p', GOPT_ARG, gopt_shorts('p'),
				       gopt_longs("port"), " <port>", "server port"),
			   gopt_option('m', GOPT_ARG, gopt_shorts('m'),
				       gopt_longs("port-admin"), " <port>", "server admin port"),
			   gopt_option('h', 0, gopt_shorts('h', '?'), gopt_longs("help"),
				       NULL, "display this help and exit"));
	void *opt = gopt_sort(&argc, (const char**)argv, opt_def);
	if (gopt(opt, 'h')) {
		printf("usage: tarantool [options] [query]\n\n");
		printf("tarantool sql client.\n");
		gopt_help(opt_def);
		gopt_free(opt);
		return 0;
	}

	/* server host */
	const char *arg = NULL;
	gopt_arg(opt, 'a', &arg);

	char host[128];
	snprintf(host, sizeof(host), "%s", (arg) ? arg : DEFAULT_HOST);

	/* server port */
	int port = DEFAULT_PORT;
	if (gopt_arg(opt, 'p', &arg))
		port = atoi(arg);

	/* server admin port */
	int admin_port = DEFAULT_PORT_ADMIN;
	if (gopt_arg(opt, 'm', &arg))
		admin_port = atoi(arg);
	gopt_free(opt);

	/* creating and initializing tarantool network stream */
	struct tnt_stream *t = tnt_net(NULL);
	if (t == NULL)
		return 1;
	tnt_set(t, TNT_OPT_HOSTNAME, host);
	tnt_set(t, TNT_OPT_PORT, port);
	tnt_set(t, TNT_OPT_SEND_BUF, 0);
	tnt_set(t, TNT_OPT_RECV_BUF, 0);

	if (tnt_init(t) == -1) {
		printf("error: %s\n", tnt_strerror(t));
		tnt_stream_free(t);
		return 1;
	}

	/* connecting to server */
	if (tnt_connect(t) == -1) {
		printf("error: %s\n", tnt_strerror(t));
		tnt_stream_free(t);
		return 1;
	}

	/* creating tarantool admin handler */
	struct tnt_admin a;
	if (tnt_admin_init(&a, host, admin_port) == -1) {
		printf("error: admin console initialization failed\n");
		tnt_stream_free(t);
		return 1;
	}

	/* main */
	int rc = 0;
	if (argc >= 2) 
		rc = run_cmdline(t, &a, argc, argv);
	else
		rc = run_interactive(t, &a, host);
	tnt_stream_free(t);
	tnt_admin_free(&a);
	return rc;
}
