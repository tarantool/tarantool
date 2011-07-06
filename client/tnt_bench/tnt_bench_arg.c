
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

#include <client/tnt_bench/tnt_bench_arg.h>

void
tnt_bench_arg_init(struct tnt_bench_arg *arg,
	           struct tnt_bench_arg_cmd *cmds, int argc, char * argv[])
{
	arg->argc = argc;
	arg->argv = argv;
	arg->cmds = cmds;
	arg->pos  = 0;
}

static struct tnt_bench_arg_cmd*
tnt_bench_arg_cmp(struct tnt_bench_arg *arg, char *argument)
{
	int iter = 0;
	for (;arg->cmds[iter].name ; iter++) {
		if (strcmp(arg->cmds[iter].name, argument) == 0)
			return (&arg->cmds[iter]);
	}
	return NULL;
}

int
tnt_bench_arg(struct tnt_bench_arg *arg, char **argp)
{
	struct tnt_bench_arg_cmd *cmd;
	for (arg->pos++ ; arg->pos < arg->argc ; arg->pos++) {
		cmd = tnt_bench_arg_cmp(arg, arg->argv[arg->pos]);
		if (cmd == NULL) {
			arg->argc = (arg->argc - arg->pos);
			arg->argv = (arg->argv + arg->pos);
			return TNT_BENCH_ARG_UNKNOWN;
		}
		if (cmd->arg == 0) {
			*argp = NULL;
			return cmd->token;
		}
		if ((arg->pos + 1) < arg->argc) {
			*argp = arg->argv[++arg->pos];
			return cmd->token;
		} else
			return TNT_BENCH_ARG_ERROR;
	}
	return TNT_BENCH_ARG_DONE;
}
