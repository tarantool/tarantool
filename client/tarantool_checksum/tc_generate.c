
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
#include "tc_file.h"

uint32_t
search_hash(void *x, const struct tc_key *k)
{
	struct tc_space *s = x;
	uint32_t h = 13;
	int i;
	for (i = 0; i < s->pk.count; i++) {
		switch (s->pk.fields[i].type) {
		case TC_SPACE_KEY_NUM: {
			assert(TC_KEY_SIZE(k, i) == 4);
			uint32_t a32 = *(uint32_t *)TC_KEY_DATA(k, i);
			h = (h << 9) ^ (h >> 23) ^ a32;
			break;
		}
		case TC_SPACE_KEY_NUM64: {
			uint64_t a64 = *(uint64_t *) (TC_KEY_DATA(k, i));
			h = (h << 9) ^ (h >> 23) ^ (uint32_t) (a64);
			h = (h << 9) ^ (h >> 23) ^ (uint32_t) (a64 >> 32);
			break;
		}
		case TC_SPACE_KEY_STRING:
			 h = MurmurHash2(TC_KEY_DATA(k, i), TC_KEY_SIZE(k, i), h);
			break;
		case TC_SPACE_KEY_UNKNOWN:
			assert(1);
			break;
		}
	}

	return h;
}

int
search_equal(void *x, const struct tc_key *a,
	     const struct tc_key *b)
{
	if (a->size != b->size)
		return 0;
	struct tc_space *s = x;
	int i;
	for (i = 0; i < s->pk.count; i++) {
		switch (s->pk.fields[i].type) {
		case TC_SPACE_KEY_NUM: {
			assert(TC_KEY_SIZE(a, i) == 4);
			assert(TC_KEY_SIZE(b, i) == 4);
			uint32_t av = *((uint32_t *) TC_KEY_DATA(a, i));
			uint32_t bv = *((uint32_t *) TC_KEY_DATA(b, i));
			if (av != bv)
				return 0;
			break;
		}
		case TC_SPACE_KEY_NUM64: {
			assert(TC_KEY_SIZE(a, i) == 8);
			assert(TC_KEY_SIZE(b, i) == 8);
			uint64_t av = *((uint64_t *) TC_KEY_DATA(a, i));
			uint64_t bv = *((uint64_t *) TC_KEY_DATA(b, i));
			if (av != bv)
				return 0;
			break;
		}
		case TC_SPACE_KEY_STRING:
			if (TC_KEY_SIZE(a, i) != TC_KEY_SIZE(b, i))
				return 0;
			if (memcmp(TC_KEY_DATA(a, i), TC_KEY_DATA(b, i),
				   TC_KEY_SIZE(a, i)) != 0)
				return 0;
			break;
		case TC_SPACE_KEY_UNKNOWN:
			assert(1);
			break;
		}
	}
	return 1;
}

static inline int
tc_generate_of(struct tnt_request *r, uint32_t *ns, struct tnt_tuple **t)
{
	switch (r->h.type) {
	case TNT_OP_INSERT:
		*ns = r->r.insert.h.ns;
		*t = &r->r.insert.t;
		return 0;
	case TNT_OP_UPDATE:
		*ns = r->r.update.h.ns;
		*t = &r->r.update.t;
		return 0;
	case TNT_OP_DELETE:
		*ns = r->r.del.h.ns;
		*t = &r->r.del.t;
		return 0;
	}
	return -1;
}

struct tc_key*
tc_generate_key(struct tc_space *s, struct tnt_tuple *t)
{
	size_t size = 0;

	/* calculate total key size */
	int i;
	for (i = 0; i < s->pk.count; i++) {
		struct tnt_iter it;
		tnt_iter(&it, t);
		if (tnt_field(&it, t, s->pk.fields[i].n) == NULL)
			return NULL;
		if (it.status != TNT_ITER_OK)
			return NULL;
		size += TNT_IFIELD_SIZE(&it);
	}

	/* allocate key */
	size_t off = s->pk.count * sizeof(struct tc_key_field);
	size = off + size;

	struct tc_key *k = malloc(sizeof(struct tc_key) + size);
	if (k == NULL)
		return NULL;
	k->size = size;
	k->crc = 0;

	/* initialize key */
	for (i = 0; i < s->pk.count; i++) {
		struct tnt_iter it;
		tnt_iter(&it, t);
		if (tnt_field(&it, t, s->pk.fields[i].n) == NULL) {
			free(k);
			tnt_iter_free(&it);
			return NULL;
		}
		if (it.status != TNT_ITER_OK) {
			free(k);
			tnt_iter_free(&it);
			return NULL;
		}
		k->i[i].size = TNT_IFIELD_SIZE(&it);
		k->i[i].offset = off;

		memcpy((char*)k + sizeof(struct tc_key) + off,
		       TNT_IFIELD_DATA(&it),
		       TNT_IFIELD_SIZE(&it));

		off += TNT_IFIELD_SIZE(&it);
		tnt_iter_free(&it);
	}

	return k;
}

static int
tc_generate_entry(struct tc_spaces *s, struct tnt_request *r)
{
	/* 1. match space */
	uint32_t ns = 0;
	struct tnt_tuple *t = NULL;
	if (tc_generate_of(r, &ns, &t) == -1) {
		printf("bad xlog operation %d\n", r->h.type);
		return -1;
	}
	struct tc_space *space = tc_space_match(s, ns);
	if (space == NULL) {
		printf("space %d is not defined\n", ns);
		return -1;
	}
	/* 2. create key */
	struct tc_key *k = tc_generate_key(space, t);
	if (k == NULL) {
		printf("failed to create key\n");
		return -1;
	}
	/* 3. put into hash */
	mh_int_t pos = mh_pk_put(space, &space->hash_log, k, NULL);
	if (pos == mh_end(&space->hash_log)) {
		free(k);
		return -1;
	}
	return 0;
}

static int
tc_generate_xlog(struct tc_spaces *s, char *wal_dir, uint64_t file_lsn,
		 uint64_t start,
		 uint64_t *last)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%020llu.xlog", wal_dir,
		 (unsigned long long) file_lsn);

	printf("(xlog) %020llu.xlog\r",
	       (unsigned long long) file_lsn);
	fflush(stdout);

	struct tnt_stream st;
	tnt_xlog(&st);
	if (tnt_xlog_open(&st, path) == -1) {
		printf("failed to open xlog file\n");
		tnt_stream_free(&st);
		return -1;
	}

	struct tnt_iter i;
	tnt_iter_request(&i, &st);
	int count = 0;
	int rc = 0;
	while (tnt_next(&i)) {
		struct tnt_request *r = TNT_IREQUEST_PTR(&i);
		struct tnt_stream_xlog *xs =
			TNT_SXLOG_CAST(TNT_IREQUEST_STREAM(&i));
		if (xs->log.current.hdr.lsn > *last)
			*last = xs->log.current.hdr.lsn;
		if (xs->log.current.hdr.lsn <= start)
			continue;
		rc = tc_generate_entry(s, r);
		if (rc == -1)
			goto done;
		if (count % 10000 == 0) {
			printf("(xlog) %020llu.xlog %.3fM processed\r",
			       (unsigned long long) file_lsn,
			       (float)count / 1000000);
			fflush(stdout);
		}
		count++;
	}
	printf("\n");
	if (i.status == TNT_ITER_FAIL) {
		printf("xlog parsing failed: %s\n", tnt_xlog_strerror(&st));
		rc = -1;
	}
done:
	tnt_iter_free(&i);
	tnt_stream_free(&st);
	return rc;
}

static int
tc_generate_waldir_xlog(struct tc_spaces *s, struct tnt_dir *wal_dir,
		        uint64_t snap_lsn, uint64_t *last,
			int i)
{
	int rc;
	if (i < wal_dir->count) {
		rc = tc_generate_xlog(s, wal_dir->path, wal_dir->files[i].lsn,
				      snap_lsn, last);
		if (rc == -1)
			return -1;
	}
	for (i++; i < wal_dir->count; i++) {
		rc = tc_generate_xlog(s, wal_dir->path, wal_dir->files[i].lsn, 0,
				      last);
		if (rc == -1)
			return -1;
	}
	return 0;
}

static int
tc_generate_waldir(struct tc_spaces *s, uint64_t last_snap_lsn,
		   uint64_t *last_xlog_lsn, char *path)
{
	/* get latest existing lsn after snapshot */
	struct tnt_dir wal_dir;
	tnt_dir_init(&wal_dir, TNT_DIR_XLOG);

	int rc = tnt_dir_scan(&wal_dir, path);
	if (rc == -1) {
		printf("failed to open wal directory\n");
		tnt_dir_free(&wal_dir);
		return -1;
	}

	/* match xlog file containling latest snapshot lsn record */
	if (last_snap_lsn == 1) {
		rc = tc_generate_waldir_xlog(s, &wal_dir, last_snap_lsn, last_xlog_lsn, 0);
		if (rc == -1) {
			tnt_dir_free(&wal_dir);
			return -1;
		}
		goto done;
	}
	uint64_t xlog_inc = 0;
	rc = tnt_dir_match_inc(&wal_dir, last_snap_lsn, &xlog_inc);
	if (rc == -1) {
		printf("failed to match xlog with snapshot lsn\n");
		tnt_dir_free(&wal_dir);
		return -1;
	}

	/* index all xlog records from xlog file (a:last_snap_lsn) to 
	 * latest existing xlog lsn */
	int i = 0;
	while (i < wal_dir.count && wal_dir.files[i].lsn != xlog_inc)
		i++;

	rc = tc_generate_waldir_xlog(s, &wal_dir, last_snap_lsn, last_xlog_lsn, i);
	if (rc == -1) {
		tnt_dir_free(&wal_dir);
		return -1;
	}
done:
	tnt_dir_free(&wal_dir);
	return 0;
}

static int
tc_generate_snaprow(struct tc_spaces *s, struct tnt_iter_storage *is,
		    struct tnt_stream_snapshot *ss)
{
	struct tc_space *space = tc_space_match(s, ss->log.current.row_snap.space);

	struct tc_key *k = tc_generate_key(space, &is->t);
	if (k == NULL) {
		printf("failed to create key\n");
		return -1;
	}

	/* foreach snapshot row which does not exist in index dump:
	 * calculate crc and add to the index */
	mh_int_t pos = mh_pk_get(space, &space->hash_log, k);
	struct tc_key *v = NULL;
	if (pos != mh_end(&space->hash_log))
		v = mh_value(&space->hash_log, pos);
	if (v == NULL) {
		k->crc = crc32c(0, (unsigned char*)is->t.data, is->t.size);
		mh_int_t pos = mh_pk_put(space, &space->hash_snap, k, NULL);
		if (pos == mh_end(&space->hash_snap)) {
			free(k);
			return -1;
		}
	} else {
		free(k);
	}

	return 0;
}

static int
tc_generate_snapshot(struct tc_spaces *s, uint64_t lsn, char *snap_dir)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%020llu.snap", snap_dir,
		 (unsigned long long) lsn);
	printf("(snapshot) %020llu.snap\n",
	       (unsigned long long) lsn);

	struct tnt_stream st;
	tnt_snapshot(&st);
	if (tnt_snapshot_open(&st, path) == -1) {
		printf("failed to open snapshot file\n");
		tnt_stream_free(&st);
		return -1;
	}
	struct tnt_iter i;
	tnt_iter_storage(&i, &st);
	int rc = 0;
	while (tnt_next(&i)) {
		struct tnt_iter_storage *is = TNT_ISTORAGE(&i);
		struct tnt_stream_snapshot *ss =
			TNT_SSNAPSHOT_CAST(TNT_IREQUEST_STREAM(&i));
		rc = tc_generate_snaprow(s, is, ss);
		if (rc == -1)
			goto done;
	}
	if (i.status == TNT_ITER_FAIL) {
		printf("snapshot parsing failed: %s\n", tnt_snapshot_strerror(&st));
		rc = -1;
	}
done:
	tnt_iter_free(&i);
	tnt_stream_free(&st);
	return rc;
}

int tc_generate(struct tc_options *opts)
{
	printf(">>> Signature file generation\n");

	/* 1. create spaces according to a configuration file */
	struct tc_spaces s;
	int rc = tc_space_init(&s);
	if (rc == -1)
		return -1;
	rc = tc_space_fill(&s, opts);
	if (rc == -1) {
		tc_space_free(&s);
		return -1;
	}
	printf("configured spaces: %d\n", mh_size(s.t));
	printf("snap_dir: %s\n", opts->cfg.snap_dir);
	printf("wal_dir: %s\n", opts->cfg.wal_dir);

	/* 2. find newest snapshot lsn */
	struct tnt_dir snap_dir;
	tnt_dir_init(&snap_dir, TNT_DIR_SNAPSHOT);

	if (opts->cfg.snap_dir == NULL) {
		printf("snapshot directory is not specified\n");
		tc_space_free(&s);
		return -1;
	}
	if (opts->cfg.wal_dir == NULL) {
		printf("xlog directory is not specified\n");
		tc_space_free(&s);
		return -1;
	}
	rc = tnt_dir_scan(&snap_dir, opts->cfg.snap_dir);
	if (rc == -1) {
		printf("failed to open snapshot directory\n");
		goto error;
	}
	uint64_t last_snap_lsn = 0;
	rc = tnt_dir_match_gt(&snap_dir, &last_snap_lsn);
	if (rc == -1) {
		printf("failed to match greatest snapshot lsn\n");
		goto error;
	}
	printf("last snapshot lsn: %llu\n",
	       (unsigned long long) last_snap_lsn);

	/* 3. 
	 *  a. get latest existing lsn after snapshot
	 *  b. index all xlogs from newest snapshot lsn to latest xlog lsn
	 */
	uint64_t last_xlog_lsn = 0;
	rc = tc_generate_waldir(&s, last_snap_lsn, &last_xlog_lsn, opts->cfg.wal_dir);
	if (rc == -1)
		goto error;
	printf("last xlog lsn: %llu\n", (unsigned long long) last_xlog_lsn);

	/* 4. build index on each snapshot row which doesnt not exist in
	 * index dump (2)
	 */
	rc = tc_generate_snapshot(&s, last_snap_lsn, opts->cfg.snap_dir);
	if (rc == -1)
		goto error;
	/* 5. save signature file with both 2 and 3 indexes */
	printf("(signature) saving %s\n", opts->file);
	rc = tc_file_save(&s, last_snap_lsn, last_xlog_lsn, (char*)opts->file);
	if (rc == -1)
		goto error;

	tnt_dir_free(&snap_dir);
	tc_space_free(&s);
	return 0;
error:
	tnt_dir_free(&snap_dir);
	tc_space_free(&s);
	return -1;
}
