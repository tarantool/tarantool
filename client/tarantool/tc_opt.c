
/*
 * Copyright (C) 2012 Mail.RU
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
#include <stdint.h>

#include <third_party/gopt/gopt.h>

#include "client/tarantool/tc_opt.h"

#define TC_DEFAULT_HOST "localhost"
#define TC_DEFAULT_PORT 33013
#define TC_DEFAULT_PORT_ADMIN 33015

/* supported cli options */
static const void *tc_options_def = gopt_start(
	gopt_option('a', GOPT_ARG, gopt_shorts('a'),
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
		    gopt_longs("rpl"), " <lsn>", "act as replica for the specified server"),
	gopt_option('h', 0, gopt_shorts('h', '?'), gopt_longs("help"),
		    NULL, "display this help and exit")
);

void tc_opt_usage(void)
{
	printf("usage: tarantool [options] [query]\n\n");
	printf("tarantool client.\n");
	gopt_help(tc_options_def);
	exit(0);
}

enum tc_opt_mode tc_opt_init(struct tc_opt *opt, int argc, char **argv)
{
	/* usage */
	void *tc_options = gopt_sort(&argc, (const char**)argv, tc_options_def);
	if (gopt(tc_options, 'h')) {
		opt->mode = TC_OPT_USAGE;
		goto done;
	}

	/* server host */
	gopt_arg(tc_options, 'a', &opt->host);
	if (opt->host == NULL)
		opt->host = TC_DEFAULT_HOST;

	/* server port */
	const char *arg = NULL;
	opt->port = TC_DEFAULT_PORT;
	if (gopt_arg(tc_options, 'p', &arg))
		opt->port = atoi(arg);

	/* server admin port */
	opt->port_admin = TC_DEFAULT_PORT_ADMIN;
	if (gopt_arg(tc_options, 'm', &arg))
		opt->port_admin = atoi(arg);

	/* replica mode */
	if (gopt_arg(tc_options, 'R', &arg)) {
		opt->mode = TC_OPT_RPL;
		opt->lsn = strtoll(arg, NULL, 10);
		goto done;
	}

	/* wal-cat mode */
	if (gopt_arg(tc_options, 'C', &opt->xlog)) {
		opt->mode = TC_OPT_WAL_CAT;
		goto done;
	}

	/* wal-play mode */
	if (gopt_arg(tc_options, 'P', &opt->xlog)) {
		opt->mode = TC_OPT_WAL_PLAY;
		goto done;
	}

	/* default */
	if (argc >= 2) {
		opt->cmdv = argv + 1;
		opt->cmdc = argc - 1;
		opt->mode = TC_OPT_CMD;
	} else {
		opt->mode = TC_OPT_INTERACTIVE;
	}
done:
	gopt_free(tc_options);
	return opt->mode;
}
