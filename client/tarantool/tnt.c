
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

#include <readline/readline.h>
#include <readline/history.h>

#include <third_party/gopt/gopt.h>
#include <connector/c/include/tnt.h>
#include <connector/c/sql/tnt_sql.h>
#include <client/tarantool/tnt_admin.h>

#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT 33013
#define DEFAULT_PORT_ADMIN 33015
#define HISTORY_FILE ".tarantool_history"

static int
query_reply(struct tnt *t)
{
	int rc = -1;
	struct tnt_recv rcv;
	tnt_recv_init(&rcv);
	if (tnt_recv(t, &rcv) == -1) {
		printf("recv failed: %s\n", tnt_strerror(t));
		goto error;
	}
	switch (TNT_RECV_OP(&rcv)) {
	case TNT_RECV_PING:
	       	printf("Ping ");
		break;
	case TNT_RECV_INSERT:
	       	printf("Insert ");
		break;
	case TNT_RECV_DELETE:
	       	printf("Delete ");
		break;
	case TNT_RECV_UPDATE:
	       	printf("Update ");
		break;
	case TNT_RECV_SELECT:
	       	printf("Select ");
		break;
	case TNT_RECV_CALL:
	       	printf("Call ");
		break;
	default:
	       	printf("Unknown ");
		break;
	}
	if (tnt_error(t) != TNT_EOK) {
		printf("FAIL, %s (op: %d, reqid: %d, code: %d, count: %d)\n",
			tnt_strerror(t), TNT_RECV_OP(&rcv),
			TNT_RECV_ID(&rcv),
			TNT_RECV_CODE(&rcv),
			TNT_RECV_COUNT(&rcv));
		if (tnt_error(t) == TNT_EERROR)
			printf("error: %s\n", tnt_recv_error(&rcv));
		goto error;
	}
	printf("OK, %d rows affected\n", TNT_RECV_COUNT(&rcv));
	struct tnt_tuple *tp;
	TNT_RECV_FOREACH(&rcv, tp) {
		printf("[");
		struct tnt_tuple_field *tf;
		TNT_TUPLE_FOREACH(tp, tf) {
			if (!isprint(tf->data[0]) && (tf->size == 4 || tf->size == 8)) {
				if (tf->size == 4) {
					uint32_t i = *((uint32_t*)tf->data);
					printf("%"PRIu32, i);
				} else {
					uint64_t i = *((uint64_t*)tf->data);
					printf("%"PRIu64, i);
				}
			} else {
				printf("'%-.*s'", tf->size, tf->data);
			}
			if (!TNT_TUPLE_LAST(tp, tf))
				printf(", ");
		}
		printf("]\n");
	}
	rc = 0;
error:
	tnt_recv_free(&rcv);
	return rc;
}

static int
query(struct tnt *t, char *q)
{
	char *e = NULL;
	int ops = tnt_query(t, q, strlen(q), &e);
	if (ops == -1) {
		if (e) {
			printf("error: %s", e);
			free(e);
		}
		return -1;
	}
	int rc = 0;
	while (ops-- > 0)
		if (query_reply(t) == -1)
			rc = -1;
	return rc;
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

	/* creating and initializing tarantool client handler */
	struct tnt *t = tnt_alloc();
	if (t == NULL)
		return 1;
	tnt_set(t, TNT_OPT_HOSTNAME, host);
	tnt_set(t, TNT_OPT_PORT, port);
	if (tnt_init(t) == -1) {
		printf("error: %s\n", tnt_strerror(t));
		tnt_free(t);
		return 1;
	}

	/* connecting to server */
	if (tnt_connect(t) == -1) {
		printf("error: %s\n", tnt_strerror(t));
		tnt_free(t);
		return 1;
	}

	/* creating tarantool admin handler */
	struct tnt_admin a;
	if (tnt_admin_init(&a, host, admin_port) == -1) {
		printf("error: admin console initialization failed\n");
		tnt_free(t);
		return 1;
	}

	/* command line mode */
	if (argc >= 2) {
		int i, rc = 0;
		for (i = 1 ; i < argc ; i++) {
			if (tnt_query_is(argv[i], strlen(argv[i]))) {
				if (query(t, argv[i]) == -1)
					rc = 1;
			} else {
				int reply = strcmp(argv[i], "exit") && strcmp(argv[i], "quit");
				if (query_admin(&a, argv[i], reply) == -1)
					rc = 1;
				if (!reply)
					break;
			}
		}
		tnt_free(t);
		tnt_admin_free(&a);
		return rc;
	}

	/* interactive mode */
	char *home = getenv("HOME");
	char history[1024];
	snprintf(history, sizeof(history), "%s/%s", home, HISTORY_FILE);
	read_history(history);

	char prompt[128];
	snprintf(prompt, sizeof(prompt), "%s> ", host);

	char *cmd;
	while ((cmd = readline(prompt))) {
		if (!cmd[0])
			continue;
		if (tnt_query_is(cmd, strlen(cmd)))
			query(t, cmd);
		else {
			int reply = strcmp(cmd, "exit") && strcmp(cmd, "quit");
			query_admin(&a, cmd, reply);
			if (!reply) {
				free(cmd);
				break;
			}
		}
		add_history(cmd);
		free(cmd);
	}

	write_history(history);
	clear_history();

	tnt_free(t);
	tnt_admin_free(&a);
	return 0;
}
