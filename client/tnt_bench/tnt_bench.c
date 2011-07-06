
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

#include <libtnt.h>

#include <client/tnt_bench/tnt_bench_stat.h>
#include <client/tnt_bench/tnt_bench_func.h>
#include <client/tnt_bench/tnt_bench_test.h>
#include <client/tnt_bench/tnt_bench_opt.h>
#include <client/tnt_bench/tnt_bench.h>
#include <client/tnt_bench/tnt_bench_cb.h>
#include <client/tnt_bench/tnt_bench_plot.h>

int
tnt_bench_init(struct tnt_bench *bench, struct tnt_bench_funcs *funcs,
	       struct tnt_bench_opt *opt)
{
	bench->funcs = funcs;
	bench->opt = opt;

	tnt_bench_test_init(&bench->tests);

	bench->t = tnt_alloc();
	if (bench->t == NULL)
		return -1;

	tnt_set(bench->t, TNT_OPT_PROTO, opt->proto);
	tnt_set(bench->t, TNT_OPT_HOSTNAME, opt->host);
	tnt_set(bench->t, TNT_OPT_PORT, opt->port);
	tnt_set(bench->t, TNT_OPT_SEND_BUF, opt->sbuf);
	tnt_set(bench->t, TNT_OPT_RECV_BUF, opt->rbuf);

	if (tnt_init(bench->t) == -1)
		return -1;
	return 0;
}

void
tnt_bench_free(struct tnt_bench *bench)
{
	tnt_bench_test_free(&bench->tests);
	tnt_free(bench->t);
}

static void
tnt_bench_set_std(struct tnt_bench *bench)
{
	struct tnt_bench_func *f;
	struct tnt_bench_test *t;

	f = tnt_bench_func_match(bench->funcs, "insert");
	t = tnt_bench_test_add(&bench->tests, f);
	tnt_bench_test_buf_add(t, 32);
	tnt_bench_test_buf_add(t, 64);
	tnt_bench_test_buf_add(t, 128);

	f = tnt_bench_func_match(bench->funcs, "insert-ret");
	t = tnt_bench_test_add(&bench->tests, f);
	tnt_bench_test_buf_add(t, 32);
	tnt_bench_test_buf_add(t, 64);
	tnt_bench_test_buf_add(t, 128);

	f = tnt_bench_func_match(bench->funcs, "update");
	t = tnt_bench_test_add(&bench->tests, f);
	tnt_bench_test_buf_add(t, 32);
	tnt_bench_test_buf_add(t, 64);
	tnt_bench_test_buf_add(t, 128);

	f = tnt_bench_func_match(bench->funcs, "update-ret");
	t = tnt_bench_test_add(&bench->tests, f);
	tnt_bench_test_buf_add(t, 32);
	tnt_bench_test_buf_add(t, 64);
	tnt_bench_test_buf_add(t, 128);

	f = tnt_bench_func_match(bench->funcs, "select");
	t = tnt_bench_test_add(&bench->tests, f);
	tnt_bench_test_buf_add(t, 0);
}

static void
tnt_bench_set_std_memcache(struct tnt_bench *bench)
{
	struct tnt_bench_func *f;
	struct tnt_bench_test *t;

	f = tnt_bench_func_match(bench->funcs, "memcache-set");
	t = tnt_bench_test_add(&bench->tests, f);
	tnt_bench_test_buf_add(t, 32);
	tnt_bench_test_buf_add(t, 64);
	tnt_bench_test_buf_add(t, 128);

	f = tnt_bench_func_match(bench->funcs, "memcache-get");
	t = tnt_bench_test_add(&bench->tests, f);
	tnt_bench_test_buf_add(t, 32);
	tnt_bench_test_buf_add(t, 64);
	tnt_bench_test_buf_add(t, 128);
}

int
tnt_bench_connect(struct tnt_bench *bench)
{
	return tnt_connect(bench->t);
}

void
tnt_bench_run(struct tnt_bench *bench)
{
	/* using specified tests, if supplied */
	if (bench->opt->tests_count) {
		struct tnt_bench_opt_arg *arg;
		STAILQ_FOREACH(arg, &bench->opt->tests, next) {
			struct tnt_bench_func *func = 
				tnt_bench_func_match(bench->funcs, arg->arg);
			if (func == NULL) {
				printf("unknown test: \"%s\", try --test-list\n", arg->arg);
				return;
			}
			tnt_bench_test_add(&bench->tests, func);
		}
		struct tnt_bench_test *test;
		STAILQ_FOREACH(arg, &bench->opt->bufs, next) {
			STAILQ_FOREACH(test, &bench->tests.list, next) {
				tnt_bench_test_buf_add(test, atoi(arg->arg));
			}
		}
	} else {
		if (bench->opt->std) 
			tnt_bench_set_std(bench);
		else
		if (bench->opt->std_memcache) 
			tnt_bench_set_std_memcache(bench);
	}

	struct tnt_bench_stat *stats =
		malloc(sizeof(struct tnt_bench_stat) * bench->opt->reps);
	if (stats == NULL)
		return;

	struct tnt_bench_test *t;
	STAILQ_FOREACH(t, &bench->tests.list, next) {
		if (bench->opt->color)
			printf("\033[22;33m%s\033[0m\n", t->func->name);
		else
			printf("%s\n", t->func->name);
		fflush(stdout);

		struct tnt_bench_test_buf *b;
		STAILQ_FOREACH(b, &t->list, next) {
			printf("  >>> [%d] ", b->buf);
			memset(stats, 0, sizeof(struct tnt_bench_stat) *
				bench->opt->reps);
			fflush(stdout);

			int r;
			for (r = 0 ; r < bench->opt->reps ; r++) {
				t->func->func(bench->t, b->buf, bench->opt->count, &stats[r]);
				printf("<%.2f %.2f> ", stats[r].rps, (float)stats[r].tm / 1000);
				fflush(stdout);
			}

			float rps = 0.0;
			unsigned long long tm = 0;
			for (r = 0 ; r < bench->opt->reps ; r++) {
				rps += stats[r].rps;
				tm += stats[r].tm;
			}

			b->avg.rps   = rps / bench->opt->reps;
			b->avg.tm    = (float)tm / 1000 / bench->opt->reps;
			b->avg.start = 0;
			b->avg.count = 0;

			printf("\n");

			if (bench->opt->color) 
				printf("  <<< (avg time \033[22;35m%.2f\033[0m sec): \033[22;32m%.2f\033[0m rps\n", 
					b->avg.tm, b->avg.rps);
			else
				printf("  <<< (avg time %.2f sec): %.2f rps\n", 
					b->avg.tm, b->avg.rps);
		}
	}

	free(stats);
	if (bench->opt->plot)
		tnt_bench_plot(bench);
}
