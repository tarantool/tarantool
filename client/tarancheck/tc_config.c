
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
#include <limits.h>
#include <unistd.h>

#include <cfg/prscfg.h>
#include <cfg/tarantool_box_cfg.h>

#include "tc_options.h"
#include "tc_config.h"

int tc_config_load(struct tc_options *opts)
{
	FILE *f = fopen(opts->file_config, "r");
	if (f == NULL) {
		printf("failed to open config file: %s\n", opts->file_config);
		return -1;
	}
	int accepted = 0,
	    skipped = 0,
	    optional = 0;
	int rc = parse_cfg_file_tarantool_cfg(&opts->cfg, f, 0,
			                      &accepted,
					      &skipped,
					      &optional);
	fclose(f);
	if (rc == -1)
		return -1;
	rc = check_cfg_tarantool_cfg(&opts->cfg);
	if (rc == -1)
		return -1;

	char workdir[PATH_MAX];

	if (opts->cfg.work_dir == NULL) {
		getcwd(workdir, PATH_MAX);
		opts->cfg.work_dir = strndup(opts->cfg.work_dir, PATH_MAX);
	}
	if (opts->cfg.snap_dir == NULL) {
		free(opts->cfg.snap_dir);
		opts->cfg.snap_dir = strndup(opts->cfg.work_dir, PATH_MAX);
		if (!opts->cfg.snap_dir)
			return -1;
	}
	if (opts->cfg.wal_dir == NULL) {
		free(opts->cfg.wal_dir);
		opts->cfg.wal_dir = strndup(opts->cfg.work_dir, PATH_MAX);
		if (!opts->cfg.wal_dir)
			return -1;
	}

	return 0;
}
