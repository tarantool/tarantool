
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
#include <limits.h>

#include <signal.h>
#include <errno.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <errcode.h>
#include <third_party/gopt/gopt.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_sql.h>
#include <connector/c/include/tarantool/tnt_net.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_rpl.h>

#include <client/tarantool/tnt_admin.h>

#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT 33013
#define DEFAULT_PORT_ADMIN 33015
#define HISTORY_FILE ".tarantool_history"

static char *query_op_type(uint32_t type) {
	switch (type) {
	case TNT_OP_PING:   return "Ping";
	case TNT_OP_INSERT: return "Insert";
	case TNT_OP_DELETE: return "Delete";
	case TNT_OP_UPDATE: return "Update";
	case TNT_OP_SELECT: return "Select";
	case TNT_OP_CALL:   return "Call";
	}
	return "Unknown";
}

static char *query_op(struct tnt_reply *r) {
	return query_op_type(r->op);
}

static void print_tuple(struct tnt_tuple *tu) {
	struct tnt_iter ifl;
	tnt_iter(&ifl, tu);
	printf("[");
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

static void print_tuple_list(struct tnt_list *l) {
	struct tnt_iter it;
	tnt_iter_list(&it, l);
	while (tnt_next(&it)) {
		struct tnt_tuple *tu = TNT_ILIST_TUPLE(&it);
		print_tuple(tu);
	}
}

static void
query_reply_show(struct tnt_reply *r)
{
	printf("%s OK, %d rows affected\n", query_op(r), r->count);
	print_tuple_list(TNT_REPLY_LIST(r));
}

static int
query_reply(struct tnt_stream *t, int show_reply)
{
	int rc = -1;
	struct tnt_iter i;
	tnt_iter_reply(&i, t);
	while (tnt_next(&i)) {
		struct tnt_reply *r = TNT_IREPLY_PTR(&i);
		if (tnt_error(t) != TNT_EOK) {
			printf("%s ERROR, %s\n", query_op(r),
			       tnt_strerror(t));
			goto error;
		} else if (r->code != 0) {
			printf("%s ERROR, %s (%s)\n", query_op(r),
			       ((r->error) ? r->error : ""), tnt_errcode_str(r->code >> 8));
			goto error;
		}
		if (show_reply)
			query_reply_show(r);

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
	if (query_reply(t, 1) == -1)
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

static int
run_wal_cat(const char *file)
{
	struct tnt_stream s;
	tnt_xlog(&s);
	if (tnt_xlog_open(&s, (char*)file) == -1) {
		tnt_stream_free(&s);
		return -1;
	}
	struct tnt_iter i;
	tnt_iter_request(&i, &s);
	while (tnt_next(&i)) {
		struct tnt_request *r = TNT_IREQUEST_PTR(&i);
		struct tnt_stream_xlog *sx = TNT_SXLOG_CAST(&s);
		printf("%s lsn: %"PRIu64", time: %f, len: %"PRIu32"\n",
		       query_op_type(r->h.type),
		       sx->hdr.lsn,
		       sx->hdr.tm, sx->hdr.len);
		switch (r->h.type) {
		case TNT_OP_INSERT:
			print_tuple(&r->r.insert.t);
			break;
		case TNT_OP_DELETE:
			print_tuple(&r->r.delete.t);
			break;
		case TNT_OP_UPDATE:
			print_tuple(&r->r.update.t);
			break;
		}
	}
	int rc = 0;
	if (i.status == TNT_ITER_FAIL) {
		printf("parsing failed: %s\n", tnt_xlog_strerror(&s));
		rc = 1;
	}
	tnt_iter_free(&i);
	tnt_stream_free(&s);
	return rc;
}

static int
run_wal_play(struct tnt_stream *t, const char *file)
{
	struct tnt_stream s;
	tnt_xlog(&s);
	if (tnt_xlog_open(&s, (char*)file) == -1) {
		tnt_stream_free(&s);
		return -1;
	}
	struct tnt_iter i;
	tnt_iter_request(&i, &s);
	while (tnt_next(&i)) {
		struct tnt_request *r = TNT_IREQUEST_PTR(&i);
		if (t->write_request(t, r) == -1) {
			printf("failed to write request\n");
			goto error;
		}
		if (query_reply(t, 0) == -1)
			goto error;
	}
	if (i.status == TNT_ITER_FAIL) {
		printf("parsing failed: %s\n", tnt_xlog_strerror(&s));
		goto error;
	}
	return 0;
error:
	tnt_iter_free(&i);
	tnt_stream_free(&s);
	return 1;
}

static int
run_replica(char *host, int port, uint64_t lsn)
{
	struct tnt_stream s;
	tnt_rpl(&s);

	struct tnt_stream *sn = tnt_rpl_net(&s);
	tnt_set(sn, TNT_OPT_HOSTNAME, host);
	tnt_set(sn, TNT_OPT_PORT, port);
	tnt_set(sn, TNT_OPT_SEND_BUF, 0);
	tnt_set(sn, TNT_OPT_RECV_BUF, 0);
	if (tnt_rpl_open(&s, lsn) == -1)
		return 1;

	struct tnt_iter i;
	tnt_iter_request(&i, &s);

	while (tnt_next(&i)) {
		struct tnt_request *r = TNT_IREQUEST_PTR(&i);
		struct tnt_stream_rpl *sr = TNT_RPL_CAST(&s);
		printf("%s lsn: %"PRIu64"\n",
		       query_op_type(r->h.type),
		       sr->hdr.lsn);
		switch (r->h.type) {
		case TNT_OP_INSERT:
			print_tuple(&r->r.insert.t);
			break;
		case TNT_OP_DELETE:
			print_tuple(&r->r.delete.t);
			break;
		case TNT_OP_UPDATE:
			print_tuple(&r->r.update.t);
			break;
		}
	}
	if (i.status == TNT_ITER_FAIL)
		printf("parsing failed\n");

	tnt_iter_free(&i);
	tnt_stream_free(&s);
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
			   gopt_option('C', GOPT_ARG, gopt_shorts('C'),
				       gopt_longs("wal-cat"), " <file>", "print xlog file content"),
			   gopt_option('P', GOPT_ARG, gopt_shorts('P'),
				       gopt_longs("wal-play"), " <file>", "replay xlog file to the specified server"),
			   gopt_option('R', GOPT_ARG, gopt_shorts('R'),
				       gopt_longs("rpl"), " <lsn>", "act as replica for to the specified server"),
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

	/* wal-cat */
	const char *arg = NULL;
	if (gopt_arg(opt, 'C', &arg))
		return run_wal_cat(arg);

	/* server host */
	gopt_arg(opt, 'a', &arg);

	char host[128];
	snprintf(host, sizeof(host), "%s", (arg) ? arg : DEFAULT_HOST);

	/* server port */
	int port = DEFAULT_PORT;
	if (gopt_arg(opt, 'p', &arg))
		port = atoi(arg);

	/* replica mode */
	if (gopt_arg(opt, 'R', &arg)) {
		uint64_t lsn = strtoll(arg, NULL, 10);
		if (lsn == LLONG_MIN || lsn == LLONG_MAX) {
			printf("bad lsn number\n");
			return 1;
		}
		return run_replica(host, port, lsn);
	}

	/* server admin port */
	int admin_port = DEFAULT_PORT_ADMIN;
	if (gopt_arg(opt, 'm', &arg))
		admin_port = atoi(arg);

	/* wal-player mode */
	const char *wal_player_file = NULL;
	gopt_arg(opt, 'P', &wal_player_file);
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

	/* wal-player mode */
	if (wal_player_file) {
		int rc = run_wal_play(t, wal_player_file);
		tnt_stream_free(t);
		return rc;
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
	tnt_admin_free(&a);
	tnt_stream_free(t);
	return rc;
}
