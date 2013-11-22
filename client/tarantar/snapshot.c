
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include <third_party/crc32.h>

#include <connector/c/include/tarantool/tnt.h>
#include <connector/c/include/tarantool/tnt_xlog.h>
#include <connector/c/include/tarantool/tnt_snapshot.h>
#include <connector/c/include/tarantool/tnt_dir.h>

#include <cfg/prscfg.h>
#include <cfg/tarantool_box_cfg.h>

#include <lib/small/region.h>

#include "key.h"
#include "hash.h"
#include "options.h"
#include "space.h"
#include "sha1.h"
#include "ref.h"
#include "ts.h"
#include "indexate.h"
#include "snapshot.h"

extern struct ts tss;

static int
ts_snapshot_write(FILE *snapshot, uint32_t space, uint64_t lsn, struct tnt_tuple *t)
{
	/* write snapshot row */
	if (fwrite(&tnt_log_marker_v11, sizeof(tnt_log_marker_v11), 1, snapshot) != 1) {
		printf("failed to write row\n");
		return -1;
	}

	struct {
		struct tnt_log_header_v11 h;
		struct tnt_log_row_snap_v11 row;
	} h = {
		.h = {
			.crc32_hdr = 0,
			.lsn = lsn,
			.tm = 0.0,
			.len = sizeof(struct tnt_log_row_snap_v11) + t->size - sizeof(uint32_t),
			.crc32_data = 0
		},
		.row = {
			.tag = 65535, /* snapshot */
			.cookie = 0,
			.space = space,
			.tuple_size = t->cardinality,
			.data_size = t->size - sizeof(uint32_t)
		}
	};

	/* calculate checksum */
	h.h.crc32_data = crc32c(0, (unsigned char*)&h.row, sizeof(h.row));
	h.h.crc32_data = crc32c(h.h.crc32_data,
							(unsigned char*)t->data + sizeof(uint32_t),
							t->size - sizeof(uint32_t));
	h.h.crc32_hdr  = crc32c(0, (unsigned char*)&h.h, sizeof(h.h));

	if (fwrite(&h, sizeof(h), 1, snapshot) != 1) {
		printf("failed to write row\n");
		return -1;
	}
	if (fwrite(t->data + sizeof(uint32_t),
			   t->size - sizeof(uint32_t), 1, snapshot) != 1) {
		printf("failed to write row\n");
		return -1;
	}

	return 0;
}

static int
ts_snapshot_xfer(FILE *snapshot, struct tnt_log *current,
                 struct ts_ref *r,
                 struct ts_key *k, uint32_t space, uint64_t lsn)
{
	int rc = tnt_log_seek(current, k->offset);
	if (rc == -1) {
		printf("failed to seek for: %s:%"PRIu64"\n", r->file, k->offset);
		return -1;
	}
	if (tnt_log_next(current) == NULL) {
		printf("failed to read: %s:%"PRIu64"\n", r->file, k->offset);
		return -1;
	}

	struct tnt_tuple *t = NULL;

	if (r->is_snap) {
		t = &current->current_value.t;
	} else {
		struct tnt_request *rp = &current->current_value.r;
		switch (rp->h.type) {
		case TNT_OP_INSERT:
			t = &rp->r.insert.t;
			break;
		case TNT_OP_DELETE_1_3:
		case TNT_OP_DELETE:
			/* skip */
			//t = &rp->r.del.t;
			return 0;
		case TNT_OP_UPDATE:
			assert(0);
			break;
		default:
			assert(0);
			break;
		}
	}

	/* write snapshot row */
	if (ts_snapshot_write(snapshot, space, lsn, t) != 0)
		return -1;

	if (r->is_snap) {
		tnt_tuple_free(t);
	} else {
		tnt_request_free(&current->current_value.r);
	}
	return 0;
}

int ts_snapshot_create(void)
{
	/* TODO:
	 *
	 * index can be sorted by file:offset to reduce io overhead, but
	 * will have unsorted index on disk. */

	unsigned long long snap_lsn = tss.last_xlog_lsn;

	if (snap_lsn == 0 || tss.last_snap_lsn == snap_lsn) {
		printf("snapshot exists, skip.\n");
		return 0;
	}

	char path[1024];
	snprintf(path, sizeof(path), "%s/%020llu.snap.inprocess", tss.snap_dir,
	         (unsigned long long) snap_lsn);

	FILE *snapshot = fopen(path, "a");
	if (snapshot == NULL) {
		printf("failed to create snapshot: %s\n", path);
		return -1;
	}

	fputs(TNT_LOG_MAGIC_SNAP, snapshot);
	fputs(TNT_LOG_VERSION, snapshot);
	fputs("\n", snapshot);

	char *current_file = NULL;
	struct tnt_log current;
	memset(&current, 0, sizeof(current));

	int count = 0;
	int rc;
	mh_int_t i;
	mh_foreach(tss.s.t, i) {
		struct ts_space *space = mh_u32ptr_node(tss.s.t, i)->val;

		mh_int_t pos = 0;
		while (pos != mh_end(space->index)) {
			if (mh_exist((space->index), pos)) {
				struct ts_key *k = *mh_pk_node(space->index, pos);
				struct ts_ref *r = ts_reftable_map(&tss.rt, k->file);

				if (count % 10000 == 0) {
					printf("( >> ) %020llu.snap %.3fM processed\r",
						   (unsigned long long) snap_lsn,
						   (float)count / 1000000);
					fflush(stdout);
				}
				count++;

				/* first, check if key has a data */
				if (k->flags & TS_KEY_WITH_DATA) {
					uint32_t size = *(uint32_t*)(k->key + space->key_size);

					struct tnt_tuple *t =
						tnt_tuple_set(NULL, k->key + space->key_size + sizeof(uint32_t), size);
					if (t == NULL) {
						printf("failed to allocate tuple\n");
						goto error;
					}

					rc = ts_snapshot_write(snapshot, space->id, snap_lsn, t);
					if (rc == -1)
						goto error;
					pos++;
					continue;
				}

				/* otherwise, load from snapshot or xlog */
				if (current_file != r->file) {
					tnt_log_close(&current);
					rc = tnt_log_open(&current, r->file,
					                  (r->is_snap ? TNT_LOG_SNAPSHOT : TNT_LOG_XLOG));
					if (rc == -1) {
						printf("failed to open file: %s\n", r->file);
						goto error;
					}
					current_file = r->file;
				}

				/* transfer from file to snapshot */
				rc = ts_snapshot_xfer(snapshot, &current, r, k, space->id, snap_lsn);
				if (rc == -1)
					goto error;

			}
			pos++;
		}
	}

	/* write eof */
	if (fwrite(&tnt_log_marker_eof_v11,
	    sizeof(tnt_log_marker_eof_v11), 1, snapshot) != 1) {
		printf("failed to write row\n");
		goto error;
	}
	if (fflush(snapshot) != 0) {
		printf("flush failed\n");
		goto error;
	}
	if (fsync(fileno(snapshot)) != 0) {
		printf("sync failed\n");
		goto error;
	}
	if (fclose(snapshot) != 0) {
		printf("failed to write row\n");
	}

	char newpath[1024];
	strncpy(newpath, path, sizeof(newpath));
	char *ext = strrchr(newpath, '.');
	*ext = 0;
	rename(path, newpath);

	tnt_log_close(&current);
	printf("\n");
	return 0;
error:
	tnt_log_close(&current);
	fclose(snapshot);
	return -1;
}
