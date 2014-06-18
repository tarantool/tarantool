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
#include "log_io.h"
#include <dirent.h>
#include <fcntl.h>

#include "fiber.h"
#include "crc32.h"
#include "fio.h"
#include "third_party/tarantool_eio.h"
#include "fiob.h"
#include "msgpuck/msgpuck.h"
#include "iproto_constants.h"
#include "scoped_guard.h"
#define MH_UNDEF 1 /* conflicts with mh_nodeids_t */
#include "recovery.h" /* for mh_cluster */
#include "vclock.h"

/*
 * marker is MsgPack fixext2
 * +--------+--------+--------+--------+
 * |  0xd5  |  type  |       data      |
 * +--------+--------+--------+--------+
 */
const log_magic_t row_marker = mp_bswap_u32(0xd5ba0bab); /* host byte order */
const log_magic_t eof_marker = mp_bswap_u32(0xd510aded); /* host byte order */
const char inprogress_suffix[] = ".inprogress";
const char v12[] = "0.12\n";

/* {{{ struct log_dir */

static inline int
log_dir_map_cmp(const struct log_meta *a, const struct log_meta *b)
{
	if (a->lsnsum != b->lsnsum)
		return a->lsnsum - b->lsnsum;
	return 0;
}

rb_gen(, log_dir_map_, log_dir_map_t, struct log_meta, link, log_dir_map_cmp)

static inline int
log_dir_lsnmap_cmp(const struct log_meta_lsn *a, const struct log_meta_lsn *b)
{
	if (a->node_id != b->node_id)
		return a->node_id - b->node_id;
	if (a->lsn != b->lsn)
		return a->lsn - b->lsn;

	if (a->meta == NULL) /* a is a key */
		return 0;

	/* logs with smaller lsnsum are first */
	if (a->meta->lsnsum != b->meta->lsnsum)
		return a->meta->lsnsum - b->meta->lsnsum;

	return 0;
}

rb_gen(, log_dir_lsnmap_, log_dir_lsnmap_t, struct log_meta_lsn, link,
       log_dir_lsnmap_cmp)

#define mh_name _nodeids
#define mh_key_t uint32_t
#define mh_node_t uint32_t
#define mh_arg_t void *
#define mh_hash(a, arg) ((*a))
#define mh_hash_key(a, arg) (a)
#define mh_eq(a, b, arg) ((*a) == (*b))
#define mh_eq_key(key, node, arg) (key == (*node))
#define MH_SOURCE 1
#include "salad/mhash.h"

int
log_dir_create(struct log_dir *dir, const char *dirname,
	       enum log_dir_type type)
{
	memset(dir, 0, sizeof(*dir));
	dir->nodeids = mh_nodeids_new();
	if (dir->nodeids == NULL)
		return -1;
	log_dir_map_new(&dir->map);
	log_dir_lsnmap_new(&dir->lsnmap);
	/* Default mode. */
	dir->mode = 0660;
	dir->dirname = strdup(dirname);
	if (type == SNAP) {
		strcpy(dir->open_wflags, "wxd");
		dir->filetype = "SNAP\n";
		dir->filename_ext = ".snap";
		dir->ignore_initial_setlsn = true;
		dir->panic_if_error = true;
	} else {
		strcpy(dir->open_wflags, "wx");
		dir->sync_is_async = true;
		dir->filetype = "XLOG\n";
		dir->filename_ext = ".xlog";
	}
	return 0;
}

static struct log_meta *
log_meta_clean(log_dir_map_t *t, struct log_meta *meta, void *arg);

void
log_dir_destroy(struct log_dir *dir)
{
	mh_nodeids_delete(dir->nodeids);
	free(dir->dirname);
	log_dir_map_iter(&dir->map, NULL, log_meta_clean, dir);
}

void
log_dir_remove_from_index(struct log_dir *dir, struct log_meta *meta)
{
	for (uint32_t i = 0; i < meta->lsn_count; i++) {
		log_dir_lsnmap_remove(&dir->lsnmap, &meta->lsns[i]);
	}
	log_dir_map_remove(&dir->map, meta);
	free(meta);
}

int
log_dir_add_to_index(struct log_dir *dir, int64_t lsnsum)
{
	struct log_meta key;
	key.lsnsum = lsnsum;
	struct log_meta *meta = log_dir_map_search(&dir->map, &key);
	if (meta != NULL) {
		meta->remove_flag = false;
		return 0;
	}

	/*
	 * Open xlog to find SETLSN
	 */
	tt_uuid uuid;
	struct log_io *wal = log_io_open_for_read(dir, lsnsum, &uuid,
						  INPROGRESS);
	if (wal == NULL)
		return -1;
	auto log_guard = make_scoped_guard([&]{
		log_io_close(&wal);
	});

	/*
	 * Find SETLSN command for xlogs (must be the first)
	 */
	struct log_io_cursor cur;
	log_io_cursor_open(&cur, wal);
	struct iproto_header row;
	if (log_io_cursor_next(&cur, &row) != 0 ||
	    row.type != IPROTO_SETLSN)
		return -2;

	/*
	 * Parse SETLSN
	 */
	uint32_t row_count = 0;
	struct log_setlsn_row *rows = log_decode_setlsn(&row, &row_count);
	auto rows_guard = make_scoped_guard([=]{
		free(rows);
	});

	/*
	 * Update indexes
	 */
	meta = (struct log_meta *) calloc(1, sizeof(*meta) +
		sizeof(*meta->lsns) * row_count);
	if (meta == NULL) {
		tnt_raise(ClientError, ER_MEMORY_ISSUE, sizeof(*meta),
			"log_dir", "meta");
	}
	auto meta_guard = make_scoped_guard([=]{
		log_dir_remove_from_index(dir, meta);
		free(meta);
	});

	meta->lsnsum = lsnsum;
	log_dir_map_insert(&dir->map, meta);

	meta->lsn_count = row_count;
	int64_t lsnsum_check = 0;
	for (uint32_t i = 0; i < row_count; i++) {
		struct log_meta_lsn *meta_lsn = &meta->lsns[i];
		meta_lsn->meta = meta;
		meta_lsn->node_id = rows[i].node_id;
		meta_lsn->lsn = rows[i].lsn;
		lsnsum_check += rows[i].lsn;
		log_dir_lsnmap_insert(&dir->lsnmap, meta_lsn);

		uint32_t k;
		k = mh_nodeids_find(dir->nodeids, rows[i].node_id, NULL);
		if (k != mh_end(dir->nodeids))
			continue;

		/* Update the set of node_ids */
		k = mh_nodeids_put(dir->nodeids, &rows[i].node_id, NULL, NULL);
		if (k == mh_end(dir->nodeids)) {
			tnt_raise(ClientError, ER_MEMORY_ISSUE, sizeof(*meta),
				"log_dir", "meta->nodeids");
		}
	}

	/*
	 * Snapshots have empty starting SETLSN table. Don't check lsnsum and
	 * use the information derived from xlog name.
	 */
	if (lsnsum_check != lsnsum && !dir->ignore_initial_setlsn)
		tnt_raise(IllegalParams, "Invalid xlog name");

	meta_guard.is_active = false;
	return 0;
}

static struct log_meta *
log_meta_mark(log_dir_map_t *t, struct log_meta *meta, void *arg)
{
	(void) t;
	(void) arg;
	meta->remove_flag = true;
	return meta;
}

static struct log_meta *
log_meta_delete(log_dir_map_t *t, struct log_meta *meta, void *arg)
{
	(void) t;
	struct log_dir *dir = (struct log_dir *) arg;
	if (meta->remove_flag) {
		log_dir_remove_from_index(dir, meta);
		return NULL;
	}

	return meta;
}

static struct log_meta *
log_meta_clean(log_dir_map_t *t, struct log_meta *meta, void *arg)
{
	(void) t;
	struct log_dir *dir = (struct log_dir *) arg;
	log_dir_remove_from_index(dir, meta);
	return NULL;
}

int
log_dir_scan(struct log_dir *dir)
{
	ssize_t ext_len = strlen(dir->filename_ext);
	DIR *dh = opendir(dir->dirname);

	if (dh == NULL) {
		say_syserror("error reading directory `%s'", dir->dirname);
		return -1;
	}
	auto log_guard = make_scoped_guard([&]{
		closedir(dh);
	});

	/* Mark all items to delete */
	log_dir_map_iter(&dir->map, NULL, log_meta_mark, dir);

	errno = 0;
	struct dirent *dent;
	while ((dent = readdir(dh)) != NULL) {

		char *ext = strchr(dent->d_name, '.');
		if (ext == NULL)
			continue;

		const char *suffix = strchr(ext + 1, '.');
		/*
		 * A valid ending is either .xlog or
		 * .xlog.inprogress, given dir->filename_ext ==
		 * 'xlog'.
		 */
		bool ext_is_ok;
		if (suffix == NULL)
			ext_is_ok = strcmp(ext, dir->filename_ext) == 0;
		else
			ext_is_ok = (strncmp(ext, dir->filename_ext,
					     ext_len) == 0 &&
				     strcmp(suffix, inprogress_suffix) == 0);
		if (!ext_is_ok)
			continue;

		long long lsnsum = strtoll(dent->d_name, &ext, 10);
		if (strncmp(ext, dir->filename_ext, ext_len) != 0) {
			/* d_name doesn't parse entirely, ignore it */
			say_warn("can't parse `%s', skipping", dent->d_name);
			continue;
		}

		if (lsnsum == LLONG_MAX || lsnsum == LLONG_MIN) {
			say_warn("can't parse `%s', skipping", dent->d_name);
			continue;
		}

		int rc = log_dir_add_to_index(dir, lsnsum);
		if (rc != 0)
			return rc;
	}

	/* Delete marked items */
	log_dir_map_iter(&dir->map, NULL, log_meta_delete, dir);

	return 0;
}

int64_t
log_dir_greatest(struct log_dir *dir)
{
	struct log_meta *meta = log_dir_map_last(&dir->map);
	if (meta == NULL)
		return -1;
	return meta->lsnsum;
}

static inline struct log_meta_lsn *
log_dir_lsnmap_lesearch(log_dir_lsnmap_t *tree, struct log_meta_lsn *key)
{
	struct log_meta_lsn *node = log_dir_lsnmap_psearch(tree, key);
	if (node == NULL || node->node_id != key->node_id)
		return NULL;

	int64_t lsn = node->lsn;
	while (1) {
		struct log_meta_lsn *next = log_dir_lsnmap_next(tree, node);
		if (next == NULL || next->node_id != key->node_id ||
				next->lsn != lsn)
			break;
		node = next;
	};
	return node;
}

static inline struct log_meta_lsn *
log_dir_lsnmap_gtsearch(log_dir_lsnmap_t *tree, struct log_meta_lsn *key)
{
	struct log_meta_lsn *node = log_dir_lsnmap_nsearch(tree, key);
	if (node == NULL || node->node_id != key->node_id)
		return NULL;

	int64_t lsn = node->lsn;
	while (1) {
		struct log_meta_lsn *prev = log_dir_lsnmap_prev(tree, node);
		if (prev == NULL || prev->node_id != key->node_id ||
				prev->lsn != lsn)
			break;
		node = prev;
	};
	return node;
}

int64_t
log_dir_next(struct log_dir *dir, struct vclock *vclock)
{
	int64_t result = INT64_MAX;
	uint32_t k;
	mh_foreach(dir->nodeids, k) {
		/*
		 * Find file where lsn <= key.lsn for given node_id
		 */
		struct log_meta_lsn key;
		key.node_id = *mh_nodeids_node(dir->nodeids, k);
		key.lsn = vclock_get(vclock, key.node_id);
		key.meta = NULL; /* this node is a key */
		if (key.lsn < 0)
			key.lsn = 0;

		struct log_meta *meta = NULL;

		/*
		 * Find tree node with greatest node.meta.lsnsum where
		 * node.node_id == key.node_id, node.lsn <= key.lsn
		 */
		struct log_meta_lsn *meta_lsn =
				log_dir_lsnmap_lesearch(&dir->lsnmap, &key);
		if (meta_lsn == NULL) {
			/*
			 * Find tree node with smallest node.meta.lsnsum where
			 * node.node_id == key.node_id, node.lsn > key.lsn
			 */
			meta_lsn = log_dir_lsnmap_gtsearch(&dir->lsnmap, &key);
			if (meta_lsn == NULL)
				return INT64_MAX; /* Not found */

			/*
			 * Take a previous file
			 */
			meta = log_dir_map_prev(&dir->map, meta_lsn->meta);
			if (meta == NULL)
				return INT64_MAX; /* Not found */
		} else {
			meta = meta_lsn->meta;
		}

		/*
		 * Find min([file.lsnsum])
		 */
		if (meta->lsnsum < result)
			result = meta->lsnsum;
	}

	return result;
}

char *
format_filename(struct log_dir *dir, int64_t lsn, enum log_suffix suffix)
{
	static __thread char filename[PATH_MAX + 1];
	const char *suffix_str = suffix == INPROGRESS ? inprogress_suffix : "";
	snprintf(filename, PATH_MAX, "%s/%020lld%s%s",
		 dir->dirname, (long long)lsn, dir->filename_ext, suffix_str);
	return filename;
}

void
log_encode_setlsn(struct iproto_header *row, const struct vclock *vclock)
{
	memset(row, 0, sizeof(*row));
	row->type = IPROTO_SETLSN;

	uint32_t cluster_size = vclock != NULL ? vclock_size(vclock) : 0;
	size_t size = 128 + cluster_size *
		(mp_sizeof_uint(UINT32_MAX) + mp_sizeof_uint(UINT64_MAX));
	char *buf = (char *) region_alloc(&fiber()->gc, size);
	char *data = buf;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, IPROTO_LSNMAP);
	data = mp_encode_map(data, cluster_size);
	if (vclock != NULL) {
		vclock_foreach(vclock, server) {
			data = mp_encode_uint(data, server.id);
			data = mp_encode_uint(data, server.lsn);
		}
	}

	assert(data <= buf + size);
	row->body[0].iov_base = buf;
	row->body[0].iov_len = (data - buf);
	row->bodycnt = 1;
}

struct log_setlsn_row *
log_decode_setlsn(struct iproto_header *row, uint32_t *p_row_count)
{
	if (row->bodycnt == 0)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "SETLSN body");
	const char *data = (const char *) row->body[0].iov_base;
	const char *d = data;
	if (mp_typeof(*data) != MP_MAP) {
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "SETLSN request body");
	}
	const char *lsnmap = NULL;
	uint32_t map_size = mp_decode_map(&d);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(*d) != MP_UINT) {
			mp_next(&d); /* key */
			mp_next(&d); /* value */
			continue;
		}
		uint8_t key = mp_decode_uint(&d);
		switch (key) {
		case IPROTO_LSNMAP:
			if (mp_typeof(*d) != MP_MAP) {
				tnt_raise(ClientError, ER_INVALID_MSGPACK,
					  "invalid LSN Map");
			}
			lsnmap = d;
			mp_next(&d);
			break;
		default:
			mp_next(&d); /* value */
		}
	}

	if (lsnmap == NULL)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "missing LSNMAP");

	d = lsnmap;
	uint32_t row_count = mp_decode_map(&d);
	struct log_setlsn_row *rows = (struct log_setlsn_row *)
			calloc(row_count, sizeof(*rows));
	if (rows == NULL) {
		tnt_raise(LoggedError, ER_MEMORY_ISSUE, sizeof(*rows),
			  "log_index", "meta");
	}

	for (uint32_t i = 0; i < row_count; i++) {
		if (mp_typeof(*d) != MP_UINT)
			tnt_raise(ClientError, ER_INVALID_MSGPACK, "LSNMAP");
		rows[i].node_id = mp_decode_uint(&d);
		if (mp_typeof(*d) != MP_UINT)
			tnt_raise(ClientError, ER_INVALID_MSGPACK, "LSNMAP");
		rows[i].lsn = mp_decode_uint(&d);
	}

	*p_row_count = row_count;
	return rows;
}

/* }}} */

/* {{{ struct log_io_cursor */

static int
row_reader(FILE *f, struct iproto_header *row)
{
	const char *data;

	/* Read fixed header */
	char fixheader[XLOG_FIXHEADER_SIZE - sizeof(log_magic_t)];
	if (fread(fixheader, sizeof(fixheader), 1, f) != 1) {
		if (feof(f))
			return 1;
error:
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "invalid fixed header");
	}

	/* Decode len, previous crc32 and row crc32 */
	data = fixheader;
	if (mp_check(&data, data + sizeof(fixheader)) != 0)
		goto error;
	data = fixheader;

	/* Read length */
	if (mp_typeof(*data) != MP_UINT)
		goto error;
	uint32_t len = mp_decode_uint(&data);
	if (len > IPROTO_BODY_LEN_MAX) {
		tnt_raise(ClientError, ER_INVALID_MSGPACK,
			  "received packet is too big");
	}

	/* Read previous crc32 */
	if (mp_typeof(*data) != MP_UINT)
		goto error;

	/* Read current crc32 */
	uint32_t crc32p = mp_decode_uint(&data);
	if (mp_typeof(*data) != MP_UINT)
		goto error;
	uint32_t crc32c = mp_decode_uint(&data);
	assert(data <= fixheader + sizeof(fixheader));
	(void) crc32p;

	/* Allocate memory for body */
	char *bodybuf = (char *) region_alloc(&fiber()->gc, len);

	/* Read header and body */
	if (fread(bodybuf, len, 1, f) != 1)
		return 1;

	/* Validate checksum */
	if (crc32_calc(0, bodybuf, len) != crc32c)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "invalid crc32");

	data = bodybuf;
	iproto_header_decode(row, &data, bodybuf + len);

	return 0;
}

int
xlog_encode_row(const struct iproto_header *row, struct iovec *iov,
		char fixheader[XLOG_FIXHEADER_SIZE])
{
	int iovcnt = iproto_header_encode(row, iov + 1) + 1;
	uint32_t len = 0;
	uint32_t crc32p = 0;
	uint32_t crc32c = 0;
	for (int i = 1; i < iovcnt; i++) {
		crc32c = crc32_calc(crc32c, (const char *) iov[i].iov_base,
				    iov[i].iov_len);
		len += iov[i].iov_len;
	}

	char *data = fixheader;
	*(log_magic_t *) data = row_marker;
	data += sizeof(row_marker);
	data = mp_encode_uint(data, len);
	/* Encode crc32 for previous row */
	data = mp_encode_uint(data, crc32p);
	/* Encode crc32 for current row */
	data = mp_encode_uint(data, crc32c);
	/* Encode padding */
	ssize_t padding = XLOG_FIXHEADER_SIZE - (data - fixheader);
	if (padding > 0)
		data = mp_encode_strl(data, padding - 1) + padding - 1;
	assert(data == fixheader + XLOG_FIXHEADER_SIZE);
	iov[0].iov_base = fixheader;
	iov[0].iov_len = XLOG_FIXHEADER_SIZE;

	assert(iovcnt <= XLOG_ROW_IOVMAX);
	return iovcnt;
}

void
log_io_cursor_open(struct log_io_cursor *i, struct log_io *l)
{
	i->log = l;
	i->row_count = 0;
	i->good_offset = ftello(l->f);
	i->eof_read  = false;
}

void
log_io_cursor_close(struct log_io_cursor *i)
{
	struct log_io *l = i->log;
	l->rows += i->row_count;
	/*
	 * Since we don't close log_io
	 * we must rewind log_io to last known
	 * good position if there was an error.
	 * Seek back to last known good offset.
	 */
	fseeko(l->f, i->good_offset, SEEK_SET);
#if 0
	region_free(&fiber()->gc);
#endif
}

/**
 * Read logfile contents using designated format, panic if
 * the log is corrupted/unreadable.
 *
 * @param i	iterator object, encapsulating log specifics.
 *
 */
int
log_io_cursor_next(struct log_io_cursor *i, struct iproto_header *row)
{
	struct log_io *l = i->log;
	log_magic_t magic;
	off_t marker_offset = 0;

	assert(i->eof_read == false);

	say_debug("log_io_cursor_next: marker:0x%016X/%zu",
		  row_marker, sizeof(row_marker));

#if 0
	/*
	 * Don't let gc pool grow too much. Yet to
	 * it before reading the next row, to make
	 * sure it's not freed along here.
	 */
	region_free_after(&fiber()->gc, 128 * 1024);
#endif

restart:
	if (marker_offset > 0)
		fseeko(l->f, marker_offset + 1, SEEK_SET);

	if (fread(&magic, sizeof(magic), 1, l->f) != 1)
		goto eof;

	while (magic != row_marker) {
		int c = fgetc(l->f);
		if (c == EOF) {
			say_debug("eof while looking for magic");
			goto eof;
		}
		magic = magic >> 8 |
			((log_magic_t) c & 0xff) << (sizeof(magic)*8 - 8);
	}
	marker_offset = ftello(l->f) - sizeof(row_marker);
	if (i->good_offset != marker_offset)
		say_warn("skipped %jd bytes after 0x%08jx offset",
			(intmax_t)(marker_offset - i->good_offset),
			(uintmax_t)i->good_offset);
	say_debug("magic found at 0x%08jx", (uintmax_t)marker_offset);

	try {
		if (row_reader(l->f, row) != 0)
			goto eof;
	} catch (Exception *e) {
		if (l->dir->panic_if_error)
			panic("failed to read row");
		say_warn("failed to read row");
		goto restart;
	}

	i->good_offset = ftello(l->f);
	i->row_count++;

	if (i->row_count % 100000 == 0)
		say_info("%.1fM rows processed", i->row_count / 1000000.);

	return 0;
eof:
	/*
	 * The only two cases of fully read file:
	 * 1. sizeof(eof_marker) > 0 and it is the last record in file
	 * 2. sizeof(eof_marker) == 0 and there is no unread data in file
	 */
	if (ftello(l->f) == i->good_offset + sizeof(eof_marker)) {
		fseeko(l->f, i->good_offset, SEEK_SET);
		if (fread(&magic, sizeof(magic), 1, l->f) != 1) {

			say_error("can't read eof marker");
		} else if (magic == eof_marker) {
			i->good_offset = ftello(l->f);
			i->eof_read = true;
		} else if (magic != row_marker) {
			say_error("eof marker is corrupt: %lu",
				  (unsigned long) magic);
		} else {
			/*
			 * Row marker at the end of a file: a sign
			 * of a corrupt log file in case of
			 * recovery, but OK in case we're in local
			 * hot standby or replication relay mode
			 * (i.e. data is being written to the
			 * file. Don't pollute the log, the
			 * condition is taken care of up the
			 * stack.
			 */
		}
	}
	/* No more rows. */
	return 1;
}

/* }}} */

int
inprogress_log_rename(struct log_io *l)
{
	char *filename = l->filename;
	char new_filename[PATH_MAX];
	char *suffix = strrchr(filename, '.');

	assert(l->is_inprogress);
	assert(suffix);
	assert(strcmp(suffix, inprogress_suffix) == 0);

	/* Create a new filename without '.inprogress' suffix. */
	memcpy(new_filename, filename, suffix - filename);
	new_filename[suffix - filename] = '\0';

	if (rename(filename, new_filename) != 0) {
		say_syserror("can't rename %s to %s", filename, new_filename);

		return -1;
	}
	l->is_inprogress = false;
	return 0;
}

int
inprogress_log_unlink(char *filename)
{
#ifndef NDEBUG
	char *suffix = strrchr(filename, '.');
	assert(suffix);
	assert(strcmp(suffix, inprogress_suffix) == 0);
#endif
	if (unlink(filename) != 0) {
		/* Don't panic if there is no such file. */
		if (errno == ENOENT)
			return 0;

		say_syserror("can't unlink %s", filename);

		return -1;
	}

	return 0;
}

/* {{{ struct log_io */

int
log_io_close(struct log_io **lptr)
{
	struct log_io *l = *lptr;
	int r;

	if (l->mode == LOG_WRITE) {
		fwrite(&eof_marker, 1, sizeof(log_magic_t), l->f);
		/*
		 * Sync the file before closing, since
		 * otherwise we can end up with a partially
		 * written file in case of a crash.
		 * Do not sync if the file is opened with O_SYNC.
		 */
		if (! strchr(l->dir->open_wflags, 's'))
			log_io_sync(l);
		if (l->is_inprogress && inprogress_log_rename(l) != 0)
			panic("can't rename 'inprogress' WAL");
	}

	r = fclose(l->f);
	if (r < 0)
		say_syserror("can't close");
	free(l);
	*lptr = NULL;
	return r;
}

/** Free log_io memory and destroy it cleanly, without side
 * effects (for use in the atfork handler).
 */
void
log_io_atfork(struct log_io **lptr)
{
	struct log_io *l = *lptr;
	if (l) {
		/*
		 * Close the file descriptor STDIO buffer does not
		 * make its way into the respective file in
		 * fclose().
		 */
		close(fileno(l->f));
		fclose(l->f);
		free(l);
		*lptr = NULL;
	}
}

static int
sync_cb(eio_req *req)
{
	if (req->result)
		say_error("%s: fsync failed, errno: %d",
			  __func__, (int) req->result);

	int fd = (intptr_t) req->data;
	close(fd);
	return 0;
}

int
log_io_sync(struct log_io *l)
{
	if (l->dir->sync_is_async) {
		int fd = dup(fileno(l->f));
		if (fd == -1) {
			say_syserror("%s: dup() failed", __func__);
			return -1;
		}
		eio_fsync(fd, 0, sync_cb, (void *) (intptr_t) fd);
	} else if (fsync(fileno(l->f)) < 0) {
		say_syserror("%s: fsync failed", l->filename);
		return -1;
	}
	return 0;
}

#define NODE_UUID_KEY "Node"

static int
log_io_write_meta(struct log_io *l, const tt_uuid *node_uuid)
{
	if (fprintf(l->f, "%s%s", l->dir->filetype, v12) < 0 ||
	    fprintf(l->f, NODE_UUID_KEY ": %s\n\n",
		    tt_uuid_str(node_uuid)) < 0) {
		return -1;
	}

	return 0;
}

/**
 * Verify that file is of the given format.
 *
 * @param l		log_io object, denoting the file to check.
 * @param[out] errmsg   set if error
 *
 * @return 0 if success, -1 on error.
 */
static int
log_io_verify_meta(struct log_io *l, tt_uuid *node_uuid,
		   const char **errmsg)
{
	char filetype[32], version[32], buf[256];
	struct log_dir *dir = l->dir;
	FILE *stream = l->f;

	if (fgets(filetype, sizeof(filetype), stream) == NULL ||
	    fgets(version, sizeof(version), stream) == NULL) {
		*errmsg = "failed to read log file header";
		goto error;
	}
	if (strcmp(dir->filetype, filetype) != 0) {
		*errmsg = "unknown filetype";
		goto error;
	}

	if (strcmp(v12, version) != 0) {
		*errmsg = "unsupported file format version";
		goto error;
	}
	for (;;) {
		if (fgets(buf, sizeof(buf), stream) == NULL) {
			*errmsg = "failed to read log file header";
			goto error;
		}
		if (strcmp(buf, "\n") == 0)
			break;

		/* Parse RFC822-like string */
		char *end = buf + strlen(buf);
		if (end > buf && *(end - 1) == '\n') *(--end) = 0; /* skip \n */
		char *key = buf;
		char *val = strchr(buf, ':');
		if (val == NULL) {
			*errmsg = "invalid meta";
			goto error;
		}
		*(val++) = 0;
		while (*val == ' ') ++val; /* skip starting spaces */

		if (strcmp(key, NODE_UUID_KEY) == 0) {
			if ((end - val) != UUID_STR_LEN ||
			    tt_uuid_from_string(val, node_uuid) != 0) {
				*errmsg = "can't parse node uuid";
				goto error;
			}
		} else {
			/* Skip unknown key */
		}
	}
	return 0;
error:
	return -1;
}

struct log_io *
log_io_open(struct log_dir *dir, enum log_mode mode, const char *filename,
	    tt_uuid *node_uuid, enum log_suffix suffix, FILE *file)
{
	struct log_io *l = NULL;
	int save_errno;
	const char *errmsg = NULL;
	/*
	 * Check fopen() result the caller first thing, to
	 * preserve the errno.
	 */
	if (file == NULL) {
		errmsg = strerror(errno);
		goto error;
	}
	l = (struct log_io *) calloc(1, sizeof(*l));
	if (l == NULL) {
		errmsg = strerror(errno);
		goto error;
	}
	l->f = file;
	strncpy(l->filename, filename, PATH_MAX);
	l->mode = mode;
	l->dir = dir;
	l->is_inprogress = suffix == INPROGRESS;
	if (mode == LOG_READ) {
		if (log_io_verify_meta(l, node_uuid, &errmsg) != 0)
			goto error;
	} else { /* LOG_WRITE */
		setvbuf(l->f, NULL, _IONBF, 0);
		if (log_io_write_meta(l, node_uuid) != 0) {
			errmsg = strerror(errno);
			goto error;
		}
	}
	return l;
error:
	save_errno = errno;
	say_error("%s: failed to open %s: %s", __func__, filename, errmsg);
	if (file)
		fclose(file);
	if (l)
		free(l);
	errno = save_errno;
	return NULL;
}

struct log_io *
log_io_open_for_read(struct log_dir *dir, int64_t lsnsum,
		     tt_uuid *node_uuid, enum log_suffix suffix)
{
	const char *filename = format_filename(dir, lsnsum, suffix);
	FILE *f = fopen(filename, "r");
	if (suffix == INPROGRESS && f == NULL) {
		filename = format_filename(dir, lsnsum, NONE);
		f = fopen(filename, "r");
	}
	return log_io_open(dir, LOG_READ, filename, node_uuid, suffix, f);
}

/**
 * In case of error, writes a message to the server log
 * and sets errno.
 */
struct log_io *
log_io_open_for_write(struct log_dir *dir, int64_t lsn, tt_uuid *node_uuid,
		      enum log_suffix suffix)
{
	char *filename;
	FILE *f;
	assert(lsn != 0);

	if (suffix == INPROGRESS) {
		/*
		 * Check whether a file with this name already exists.
		 * We don't overwrite existing files.
		 */
		filename = format_filename(dir, lsn, NONE);
		if (access(filename, F_OK) == 0) {
			errno = EEXIST;
			goto error;
		}
	}
	filename = format_filename(dir, lsn, suffix);
	/*
	 * Open the <lsn>.<suffix>.inprogress file. If it exists,
	 * open will fail.
	 */
	f = fiob_open(filename, dir->open_wflags);
	if (!f)
		goto error;
	say_info("creating `%s'", filename);
	return log_io_open(dir, LOG_WRITE, filename, node_uuid, suffix, f);
error:
	say_syserror("%s: failed to open `%s'", __func__, filename);
	return NULL;
}

/* }}} */

