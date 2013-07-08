
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

#include "tc_key.h"
#include "tc_hash.h"
#include "tc_options.h"
#include "tc_config.h"
#include "tc_space.h"
#include "tc_file.h"

struct tc_file_header {
	uint32_t magic;
	uint32_t version;
	uint64_t last_xlog_lsn;
	uint64_t last_snap_lsn;
	uint32_t spaces;
	uint32_t data_offset;
};

struct tc_file_header_space {
	uint32_t space;
	uint64_t count_log;
	uint64_t count_snap;
	uint32_t data_offset;
};

int tc_file_save_space(struct tc_space *s, FILE *f)
{
	struct tc_file_header_space h = {
		.space = s->id,
		.count_log = s->hash_log->size,
		.count_snap = s->hash_snap->size,
		.data_offset = 0
	};

	fwrite(&h, sizeof(struct tc_file_header_space), 1, f);

	mh_int_t pos = 0;
	while (pos != mh_end(s->hash_log)) {
		if (mh_exist((s->hash_log), pos)) {
			const struct tc_key *k = *mh_pk_node(s->hash_log, pos);
			if (fwrite((char*)k, sizeof(struct tc_key) + k->size, 1, f) != 1)
				return -1;
		}
		pos++;
	}
	pos = 0;
	while (pos != mh_end(s->hash_snap)) {
		if (mh_exist((s->hash_snap), pos)) {
			const struct tc_key *k = *mh_pk_node(s->hash_snap, pos);
			if (fwrite((char*)k, sizeof(struct tc_key) + k->size, 1, f) != 1)
				return -1;
		}
		pos++;
	}

	return 0;
}

#define TC_FILE_MAGIC 0x123456

int tc_file_save(struct tc_spaces *s,
		 uint64_t last_snap_lsn,
		 uint64_t last_xlog_lsn, char *file)
{
	struct tc_file_header h = {
		.magic = TC_FILE_MAGIC,
		.version = 1,
		.last_xlog_lsn = last_xlog_lsn,
		.last_snap_lsn = last_snap_lsn,
		.spaces = s->t->size,
		.data_offset = 0
	};

	FILE *f = fopen(file, "w+");
	if (f == NULL)
		return -1;

	fwrite(&h, sizeof(struct tc_file_header), 1, f);

	mh_int_t pos = 0;
	while (pos != mh_end(s->t)) {
		if (mh_exist((s->t), pos)) {
			struct tc_space *space = mh_u32ptr_node(s->t, pos)->val;
			int rc = tc_file_save_space(space, f);
			if (rc == -1) {
				fclose(f);
				return -1;
			}
		}
		pos++;
	}

	fclose(f);
	return 0;
}

struct tc_key *tc_file_load_key(FILE *f)
{
	struct tc_key kh;
	if (fread(&kh, sizeof(struct tc_key), 1, f) != 1)
		return NULL;
	struct tc_key *k = malloc(sizeof(struct tc_key) + kh.size);
	if (k == NULL)
		return NULL;
	memcpy(k, &kh, sizeof(struct tc_key));
	if (fread((char*)k + sizeof(struct tc_key), kh.size, 1, f) != 1) {
		free(k);
		return NULL;
	}
	return k;
}

int tc_file_load(struct tc_spaces *s, char *file,
		 uint64_t *last_xlog_lsn, uint64_t *last_snap_lsn)
{
	struct tc_file_header h;
	memset(&h, 0, sizeof(h));

	FILE *f = fopen(file, "r");
	if (f == NULL)
		return -1;
	if (fread(&h, sizeof(struct tc_file_header), 1, f) != 1) {
		fclose(f);
		return -1;
	}
	if (h.magic != TC_FILE_MAGIC) {
		fclose(f);
		return -1;
	}

	int i = 0;
	for (; i < h.spaces; i++) {
		struct tc_file_header_space sh;
		if (fread(&sh, sizeof(struct tc_file_header_space), 1, f) != 1) {
			fclose(f);
			return -1;
		}
		struct tc_space *space = tc_space_match(s, sh.space);
		if (space == NULL) {
			fclose(f);
			return -1;
		}

		uint64_t c = 0;
		while (c < sh.count_log) {
			struct tc_key *k = tc_file_load_key(f);
			if (k == NULL) {
				fclose(f);
				return -1;
			}
			const struct tc_key *node = k;
			mh_int_t pos = mh_pk_put(space->hash_log, &node, NULL,
						 space);
			if (pos == mh_end(space->hash_log)) {
				fclose(f);
				return -1;
			}
			c++;
		}

		c = 0;
		while (c < sh.count_snap) {
			struct tc_key *k = tc_file_load_key(f);
			if (k == NULL) {
				fclose(f);
				return -1;
			}
			const struct tc_key *node = k;
			mh_int_t pos = mh_pk_put(space->hash_snap, &node,
						 NULL, space);
			if (pos == mh_end(space->hash_log)) {
				fclose(f);
				return -1;
			}
			c++;
		}
	}

	*last_xlog_lsn = h.last_xlog_lsn;
	*last_snap_lsn = h.last_snap_lsn;

	fclose(f);
	return 0;
}
