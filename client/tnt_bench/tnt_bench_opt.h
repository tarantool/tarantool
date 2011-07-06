#ifndef TNT_BENCH_OPT_H_INCLUDED
#define TNT_BENCH_OPT_H_INCLUDED

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


struct tnt_bench_opt_arg {
	char * arg;
	STAILQ_ENTRY(tnt_bench_opt_arg) next;
};

struct tnt_bench_opt {
	char *host;
	int port;
	enum tnt_proto proto;
	int rbuf;
	int sbuf;
	int count;
	int reps;
	int color;
	int plot;
	char *plot_dir;
	int std;
	int std_memcache;
	STAILQ_HEAD(,tnt_bench_opt_arg) tests;
	int tests_count;
	STAILQ_HEAD(,tnt_bench_opt_arg) bufs;
	int bufs_count;
};

void tnt_bench_opt_init(struct tnt_bench_opt *opt);
void tnt_bench_opt_free(struct tnt_bench_opt *opt);

#endif /* TNT_BENCH_OPT_H_INCLUDED */
