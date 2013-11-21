
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
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#ifdef __linux__
#  include <malloc.h>
#endif

#include <connector/c/include/tarantool/tnt.h>

#include <cfg/prscfg.h>
#include <cfg/tarantool_box_cfg.h>

#include <lib/small/region.h>

#define MH_SOURCE 1
#include "key.h"
#include "hash.h"
#include "options.h"
#include "config.h"
#include "space.h"
#include "ref.h"
#include "ts.h"
#include "indexate.h"
#include "snapshot.h"

struct ts tss;

static void
ts_init(void)
{
	ts_options_init(&tss.opts);
	memset(&tss.s, 0, sizeof(tss.s));
	memset(&tss.ra, 0, sizeof(tss.ra));
	memset(&tss.sc, 0, sizeof(tss.sc));
}

static int
ts_prepare(void)
{
	int rc = ts_reftable_init(&tss.rt);
	if (rc == -1)
		return -1;
	tss.last_snap_lsn = 0;
	tss.last_xlog_lsn = 0;
	slab_cache_create(&tss.sc);
	region_create(&tss.ra, &tss.sc);
	return 0;
}

static void
ts_free(void)
{
	ts_reftable_free(&tss.rt);
	region_free(&tss.ra);
	slab_cache_destroy(&tss.sc);
}

static void
ts_shutdown(void)
{
	ts_space_free(&tss.s);
	ts_options_free(&tss.opts);
	ts_free();
}

void
ts_oomcheck(void)
{
#ifdef __linux__
	struct mallinfo mi = mallinfo();
	if (tss.opts.limit > 0 && mi.uordblks > tss.opts.limit) {
		printf("\nmemory limit reached (%"PRIu64")\n", tss.opts.limit);
		exit(2);
	}
	return;
#else
	if (tss.opts.limit > 0 && tss.alloc > tss.opts.limit) {
		printf("\nmemory limit reached (%"PRIu64")\n", tss.opts.limit);
		exit(2);
	}
#endif
}

int main(int argc, char *argv[])
{
	ts_init();
	/* parse arguments */
	switch (ts_options_process(&tss.opts, argc, argv)) {
	case TS_MODE_USAGE:
		ts_options_free(&tss.opts);
		return ts_options_usage();
	case TS_MODE_VERSION:
		ts_options_free(&tss.opts);
		return ts_options_version();
	case TS_MODE_CREATE:
		break;
	}

	/* load configuration file */
	int rc = ts_config_load(&tss.opts);
	if (rc == -1) {
		ts_options_free(&tss.opts);
		return 1;
	}

	char cur_dir_snap[PATH_MAX], cur_dir_wal[PATH_MAX];
	tss.snap_dir = cur_dir_snap;
	tss.wal_dir  = cur_dir_wal;

	if (tss.opts.cfg.work_dir != NULL) {
		strncpy((char *)tss.snap_dir, tss.opts.cfg.work_dir, PATH_MAX);
		strncpy((char *)tss.wal_dir, tss.opts.cfg.work_dir, PATH_MAX);
	} else {
		getcwd((char *)tss.snap_dir, PATH_MAX);
		getcwd((char *)tss.wal_dir, PATH_MAX);
	}
	if (tss.opts.cfg.snap_dir != NULL) {
		if (tss.opts.cfg.snap_dir[0] == '/')
			tss.snap_dir = tss.opts.cfg.snap_dir;
		else
			strncat((char *)tss.snap_dir, tss.opts.cfg.snap_dir,
				PATH_MAX);
	}
	if (tss.opts.cfg.wal_dir != NULL) {
		if (tss.opts.cfg.wal_dir[0] == '/')
			tss.wal_dir = tss.opts.cfg.wal_dir;
		else
			strncat((char *)tss.wal_dir, tss.opts.cfg.wal_dir,
				PATH_MAX);
	}
	/* create spaces */
	rc = ts_space_init(&tss.s);
	if (rc == -1) {
		ts_space_free(&tss.s);
		ts_options_free(&tss.opts);
		return 1;
	}
	rc = ts_space_fill(&tss.s, &tss.opts);
	if (rc == -1) {
		ts_space_free(&tss.s);
		ts_options_free(&tss.opts);
		return 1;
	}

	printf("snap_dir: %s\n", tss.snap_dir);
	printf("wal_dir:  %s\n", tss.wal_dir);
	printf("spaces:   %d\n", mh_size(tss.s.t));
	printf("interval: %d\n", tss.opts.interval);
	printf("memory_limit: %dM\n", (int)(tss.opts.limit / 1024 / 1024));

	do {
		time_t tm = time(NULL);
		printf("\nSTART SNAPSHOTTING %s\n", ctime(&tm));
		rc = ts_prepare();
		if (rc == -1)
			goto done;
		/* indexate snapshot and xlog data */
		rc = ts_indexate();
		if (rc == -1)
			goto done;
		/* write snapshot */
		rc = ts_snapshot_create();
		if (rc == -1)
			goto done;
		ts_free();
		ts_space_recycle(&tss.s);
		sleep(tss.opts.interval);
	} while (tss.opts.interval > 0);
done:
	ts_shutdown();
	return (rc == -1 ? 1 : 0);
}
