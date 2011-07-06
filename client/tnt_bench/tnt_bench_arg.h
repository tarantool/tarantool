#ifndef TNT_BENCH_ARG_H_INCLUDED
#define TNT_BENCH_ARG_H_INCLUDED

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

enum {
	TNT_BENCH_ARG_DONE,
	TNT_BENCH_ARG_ERROR,
	TNT_BENCH_ARG_UNKNOWN,
	TNT_BENCH_ARG_SERVER_HOST,
	TNT_BENCH_ARG_SERVER_PORT,
	TNT_BENCH_ARG_BUF_RECV,
	TNT_BENCH_ARG_BUF_SEND,
	TNT_BENCH_ARG_AUTH_TYPE,
	TNT_BENCH_ARG_AUTH_ID,
	TNT_BENCH_ARG_AUTH_KEY,
	TNT_BENCH_ARG_AUTH_MECH,
	TNT_BENCH_ARG_TEST_STD,
	TNT_BENCH_ARG_TEST_STD_MC,
	TNT_BENCH_ARG_TEST,
	TNT_BENCH_ARG_TEST_BUF,
	TNT_BENCH_ARG_TEST_LIST,
	TNT_BENCH_ARG_COUNT,
	TNT_BENCH_ARG_REP,
	TNT_BENCH_ARG_COLOR,
	TNT_BENCH_ARG_PLOT,
	TNT_BENCH_ARG_PLOT_DIR,
	TNT_BENCH_ARG_HELP
};

struct tnt_bench_arg_cmd {
	char *name;
	int arg;
	int token;
};

struct tnt_bench_arg {
	int pos;
	int argc;
	char **argv;
	struct tnt_bench_arg_cmd *cmds;
};

void tnt_bench_arg_init(struct tnt_bench_arg *arg,
		        struct tnt_bench_arg_cmd *cmds, int argc, char * argv[]);

int tnt_bench_arg(struct tnt_bench_arg *arg, char **argp);

#endif /* TNT_BENCH_ARG_H_INCLUDED */
