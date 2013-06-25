
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
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <third_party/gopt/gopt.h>

#include <cfg/prscfg.h>
#include <cfg/tarantool_box_cfg.h>

#include "options.h"

static const void *opts_def = gopt_start(
	gopt_option('c', 0, gopt_shorts('c'),
		    gopt_longs("create"), NULL, "create snapshot file"),
	gopt_option('?', 0, gopt_shorts(0), gopt_longs("help"),
		    NULL, "display this help and exit"),
	gopt_option('v', 0, gopt_shorts('v'), gopt_longs("version"),
		    NULL, "display version information and exit")
);

void ts_options_init(struct ts_options *opts) {
	memset(opts, 0, sizeof(struct ts_options));
	init_tarantool_cfg(&opts->cfg);
}

void ts_options_free(struct ts_options *opts) {
	destroy_tarantool_cfg(&opts->cfg);
}

int ts_options_usage(void)
{
	printf("usage: tarantool_snap <options> <tarantool_config>\n\n");
	printf("tarantool snap.\n");
	gopt_help(opts_def);
	return 1;
}

enum ts_options_mode
ts_options_process(struct ts_options *opts, int argc, char **argv)
{
	void *opt = gopt_sort(&argc, (const char**)argv, opts_def);
	/* usage */
	if (gopt(opt, '?') || argc != 2) {
		opts->mode = TS_MODE_USAGE;
		goto done;
	}
	/* version */
	if (gopt(opt, 'v')) {
		opts->mode = TS_MODE_VERSION;
		goto done;
	}
	/* generate or verify */
	if (gopt(opt, 'c')) {
		opts->mode = TS_MODE_CREATE;
	} else {
		opts->mode = TS_MODE_USAGE;
		goto done;
	}
	opts->file_config = argv[1];
done:	
	gopt_free(opt);
	return opts->mode;
}
