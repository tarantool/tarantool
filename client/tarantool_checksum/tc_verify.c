
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
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_snapshot.h>
#include <connector/c/include/tarantool/tnt_dir.h>

#include <cfg/prscfg.h>
#include <cfg/tarantool_box_cfg.h>

#include <third_party/murmur_hash2.c>
#include <third_party/crc32.h>

#include "tc_key.h"
#include "tc_hash.h"
#include "tc_options.h"
#include "tc_config.h"
#include "tc_space.h"
#include "tc_generate.h"
#include "tc_verify.h"
#include "tc_file.h"

int tc_verify_cmp(struct tc_spaces *s,
		  uint64_t lsn,
		  struct tnt_iter_storage *is,
		  struct tnt_stream_snapshot *ss)
{
	struct tc_space *space =
		tc_space_match(s, ss->log.current.row_snap.space);
	if (space == NULL)
		return -1;
	int rc = 0;
	struct tc_key *k = tc_generate_key(space, &is->t);
	if (k == NULL) {
		printf("failed to create key\n");
		rc = -1;
		goto done;
	}

	/* 1. check in hash, if found then skip */
	mh_int_t pos = mh_pk_get(space, &space->hash_log, k);
	struct tc_key *v = NULL;
	if (pos != mh_end(&space->hash_log))
		v = mh_value(&space->hash_log, pos);
	if (v) {
		rc = 0;
		goto done;
	}

	/* 2. if key was not found in xlog hash, then try snapshot hash */
	pos = mh_pk_get(space, &space->hash_snap, k);
	v = NULL;
	if (pos != mh_end(&space->hash_snap))
		v = mh_value(&space->hash_snap, pos);
	if (v) {
		/* generate and compare checksum */
		uint32_t crc = crc32c(0, (unsigned char*)is->t.data, is->t.size);
		if (crc != v->crc) {
			printf("(snapshot %"PRIu64") checksum missmatch\n", lsn);
			rc = -1;
		}
	} else {
		/* not found */
		printf("(snapshot %"PRIu64") key missed\n", lsn);
		rc = -1;
	}
done:
	free(k);
	return rc; 
}

int tc_verify_process(struct tc_spaces *s, uint64_t lsn, char *snap_dir)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%020llu.snap", snap_dir,
		(long long unsigned)lsn);

	printf("(snapshot) %s\n", path);

	struct tnt_stream st;
	tnt_snapshot(&st);
	if (tnt_snapshot_open(&st, path) == -1) {
		printf("failed to open snapshot file\n");
		tnt_stream_free(&st);
		return -1;
	}

	struct tnt_iter i;
	tnt_iter_storage(&i, &st);
	int errors = 0;
	int rc = 0;
	while (tnt_next(&i)) {
		struct tnt_iter_storage *is = TNT_ISTORAGE(&i);
		struct tnt_stream_snapshot *ss =
			TNT_SSNAPSHOT_CAST(TNT_IREQUEST_STREAM(&i));
		int result = tc_verify_cmp(s, lsn, is, ss);
		if (result == -1)
			errors++;
	}
	if (i.status == TNT_ITER_FAIL) {
		printf("snapshot parsing failed: %s\n", tnt_snapshot_strerror(&st));
		rc = -1;
	}
	if (errors)
		rc = -1;
	tnt_iter_free(&i);
	tnt_stream_free(&st);
	return rc;
}

int tc_verify_match(struct tc_spaces *ss, uint64_t last_xlog_lsn,
		    uint64_t last_snap_lsn,
		    char *path)
{
	struct tnt_dir snap_dir;
	tnt_dir_init(&snap_dir, TNT_DIR_SNAPSHOT);

	int rc = tnt_dir_scan(&snap_dir, path);
	if (rc == -1) {
		printf("failed to open snap directory\n");
		tnt_dir_free(&snap_dir);
		return -1;
	}
	int i;
	for (i = 0; i < snap_dir.count; i++) {
		if (snap_dir.files[i].lsn >= last_snap_lsn && 
		    snap_dir.files[i].lsn <= last_xlog_lsn)
			break;
	}
	if (i == snap_dir.count) {
		printf("no suitable snapshot found (lsn >= %"PRIu64" && lsn <= %"PRIu64")\n",
		       last_snap_lsn, last_xlog_lsn);
		tnt_dir_free(&snap_dir);
		return -1;
	}

	rc = tc_verify_process(ss,  snap_dir.files[i].lsn, path);

	tnt_dir_free(&snap_dir);
	return rc;
}

int tc_verify(struct tc_options *opts)
{
	printf(">>> Signature file verification\n");

	/* 1. create spaces according to a configuration file */
	struct tc_spaces ss;
	int rc = tc_space_init(&ss);
	if (rc == -1)
		return -1;
	rc = tc_space_fill(&ss, opts);
	if (rc == -1)
		goto error;

	/* 2. load signature file */
	uint64_t last_xlog_lsn = 0;
	uint64_t last_snap_lsn = 0;
	rc = tc_file_load(&ss, (char*)opts->file, &last_xlog_lsn,
			  &last_snap_lsn);
	if (rc == -1)
		goto error;

	printf("(signature) loading %s\n", opts->file);

	printf("configured spaces: %"PRIu32"\n", mh_size(ss.t));
	printf("last xlog lsn: %"PRIu64"\n", last_xlog_lsn);
	printf("last snapshot lsn: %"PRIu64"\n", last_snap_lsn);
	
	/* 3. start verification process */
	rc = tc_verify_match(&ss, last_xlog_lsn, last_snap_lsn,
			     opts->cfg.snap_dir);

	printf("%s\n", (rc == 0) ? "OK": "FAILED");

	tc_space_free(&ss);
	return rc;
error:	
	tc_space_free(&ss);
	return -1;
}
