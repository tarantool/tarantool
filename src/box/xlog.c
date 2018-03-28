/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
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
#include "xlog.h"
#include <dirent.h>
#include <fcntl.h>
#include <ctype.h>

#include "fiber.h"
#include "exception.h"
#include "crc32.h"
#include "fio.h"
#include "third_party/tarantool_eio.h"
#include <msgpuck.h>

#include "coio_file.h"

#include "error.h"
#include "xrow.h"
#include "iproto_constants.h"
#include "errinj.h"

/*
 * marker is MsgPack fixext2
 * +--------+--------+--------+--------+
 * |  0xd5  |  type  |       data      |
 * +--------+--------+--------+--------+
 */
typedef uint32_t log_magic_t;

static const log_magic_t row_marker = mp_bswap_u32(0xd5ba0bab); /* host byte order */
static const log_magic_t zrow_marker = mp_bswap_u32(0xd5ba0bba); /* host byte order */
static const log_magic_t eof_marker = mp_bswap_u32(0xd510aded); /* host byte order */
static const char inprogress_suffix[] = ".inprogress";

enum {
	/**
	 * When the number of rows in xlog_tx write buffer
	 * gets this big, don't delay flush any longer, and
	 * issue a write.
	 * This also acts as a default for slab size in the
	 * slab cache so must be a power of 2.
	 */
	XLOG_TX_AUTOCOMMIT_THRESHOLD = 128 * 1024,
	/**
	 * Compress output buffer before dumping it to
	 * disk if it is at least this big. On smaller
	 * sizes compression takes up CPU but doesn't
	 * yield seizable gains.
	 * Maybe this should be a configuration option.
	 */
	XLOG_TX_COMPRESS_THRESHOLD = 2 * 1024,
};

/* {{{ struct xlog_meta */

enum {
	/*
	 * The maximum length of xlog meta
	 *
	 * @sa xlog_meta_parse()
	 */
	XLOG_META_LEN_MAX = 1024 + VCLOCK_STR_LEN_MAX
};

#define INSTANCE_UUID_KEY "Instance"
#define INSTANCE_UUID_KEY_V12 "Server"
#define VCLOCK_KEY "VClock"
#define VERSION_KEY "Version"

static const char v13[] = "0.13";
static const char v12[] = "0.12";

/**
 * Format xlog metadata into @a buf of size @a size
 *
 * @param buf buffer to use.
 * @param size the size of buffer. This function write at most @a size bytes.
 * @retval < size the number of characters printed (excluding the null byte)
 * @retval >=size the number of characters (excluding the null byte),
 *                which would have been written to the final string if
 *                enough space had been available.
 * @retval -1 error, check diag
 * @sa snprintf()
 */
static int
xlog_meta_format(const struct xlog_meta *meta, char *buf, int size)
{
	char *vstr = vclock_to_string(&meta->vclock);
	if (vstr == NULL)
		return -1;
	char *instance_uuid = tt_uuid_str(&meta->instance_uuid);
	int total = snprintf(buf, size,
		"%s\n"
		"%s\n"
		VERSION_KEY ": %s\n"
		INSTANCE_UUID_KEY ": %s\n"
		VCLOCK_KEY ": %s\n\n",
		meta->filetype, v13, PACKAGE_VERSION, instance_uuid, vstr);
	assert(total > 0);
	free(vstr);
	return total;
}

/**
 * Parse xlog meta from buffer, update buffer read
 * position in case of success
 *
 * @retval 0 for success
 * @retval -1 for parse error
 * @retval 1 if buffer hasn't enough data
 */
static ssize_t
xlog_meta_parse(struct xlog_meta *meta, const char **data,
		const char *data_end)
{
	memset(meta, 0, sizeof(*meta));
	const char *end = (const char *)memmem(*data, data_end - *data,
					       "\n\n", 2);
	if (end == NULL)
		return 1;
	++end; /* include the trailing \n to simplify the checks */
	const char *pos = (const char *)*data;

	/*
	 * Parse filetype, i.e "SNAP" or "XLOG"
	 */
	const char *eol = (const char *)memchr(pos, '\n', end - pos);
	if (eol == end || (eol - pos) >= (ptrdiff_t) sizeof(meta->filetype)) {
		diag_set(XlogError, "failed to parse xlog type string");
		return -1;
	}
	memcpy(meta->filetype, pos, eol - pos);
	meta->filetype[eol - pos] = '\0';
	pos = eol + 1;
	assert(pos <= end);

	/*
	 * Parse version string, i.e. "0.12" or "0.13"
	 */
	char version[10];
	eol = (const char *)memchr(pos, '\n', end - pos);
	if (eol == end || (eol - pos) >= (ptrdiff_t) sizeof(version)) {
		diag_set(XlogError, "failed to parse xlog version string");
		return -1;
	}
	memcpy(version, pos, eol - pos);
	version[eol - pos] = '\0';
	pos = eol + 1;
	assert(pos <= end);
	if (strncmp(version, v12, sizeof(v12)) != 0 &&
	    strncmp(version, v13, sizeof(v13)) != 0) {
		diag_set(XlogError,
			  "unsupported file format version %s",
			  version);
		return -1;
	}

	/*
	 * Parse "key: value" pairs
	 */
	while (pos < end) {
		eol = (const char *)memchr(pos, '\n', end - pos);
		assert(eol <= end);
		const char *key = pos;
		const char *key_end = (const char *)
			memchr(key, ':', eol - key);
		if (key_end == NULL) {
			diag_set(XlogError, "can't extract meta value");
			return -1;
		}
		const char *val = key_end + 1;
		/* Skip space after colon */
		while (*val == ' ' || *val == '\t')
			++val;
		const char *val_end = eol;
		assert(val <= val_end);
		pos = eol + 1;

		if (memcmp(key, INSTANCE_UUID_KEY, key_end - key) == 0 ||
		    memcmp(key, INSTANCE_UUID_KEY_V12, key_end - key) == 0) {
			/*
			 * Instance: <uuid>
			 */
			if (val_end - val != UUID_STR_LEN) {
				diag_set(XlogError, "can't parse instance UUID");
				return -1;
			}
			char uuid[UUID_STR_LEN + 1];
			memcpy(uuid, val, UUID_STR_LEN);
			uuid[UUID_STR_LEN] = '\0';
			if (tt_uuid_from_string(uuid, &meta->instance_uuid) != 0) {
				diag_set(XlogError, "can't parse instance UUID");
				return -1;
			}
		} else if (memcmp(key, VCLOCK_KEY, key_end - key) == 0){
			/*
			 * VClock: <vclock>
			 */
			if (val_end - val > VCLOCK_STR_LEN_MAX) {
				diag_set(XlogError, "can't parse vclock");
				return -1;
			}
			char vclock[VCLOCK_STR_LEN_MAX + 1];
			memcpy(vclock, val, val_end - val);
			vclock[val_end - val] = '\0';
			size_t off = vclock_from_string(&meta->vclock, vclock);
			ERROR_INJECT(ERRINJ_XLOG_META, {
				off = 1;});
			if (off != 0) {
				diag_set(XlogError, "invalid vclock at "
					  "offset %zd", off);
				return -1;
			}
		} else if (memcmp(key, VERSION_KEY, key_end - key) == 0) {
			/* Ignore Version: for now */
		} else {
			/*
			 * Unknown key
			 */
			say_warn("Unknown meta item: `%.*s'", key_end - key,
				 key);
		}
	}
	*data = end + 1; /* skip the last trailing \n of \n\n sequence */
	return 0;
}

/* struct xlog }}} */

/* {{{ struct xdir */

/* sync snapshot every 16MB */
#define SNAP_SYNC_INTERVAL	(1 << 24)

void
xdir_create(struct xdir *dir, const char *dirname,
	    enum xdir_type type, const struct tt_uuid *instance_uuid)
{
	memset(dir, 0, sizeof(*dir));
	vclockset_new(&dir->index);
	/* Default mode. */
	dir->mode = 0660;
	dir->instance_uuid = instance_uuid;
	snprintf(dir->dirname, PATH_MAX, "%s", dirname);
	dir->open_wflags = 0;
	switch (type) {
	case SNAP:
		dir->filetype = "SNAP";
		dir->filename_ext = ".snap";
		dir->suffix = INPROGRESS;
		dir->sync_interval = SNAP_SYNC_INTERVAL;
		break;
	case XLOG:
		dir->sync_is_async = true;
		dir->filetype = "XLOG";
		dir->filename_ext = ".xlog";
		dir->suffix = NONE;
		dir->force_recovery = true;
		break;
	case VYLOG:
		dir->filetype = "VYLOG";
		dir->filename_ext = ".vylog";
		dir->suffix = INPROGRESS;
		break;
	default:
		unreachable();
	}
	dir->type = type;
}

/**
 * Delete all members from the set of vector clocks.
 */
static void
vclockset_reset(vclockset_t *set)
{
	struct vclock *vclock = vclockset_first(set);
	while (vclock != NULL) {
		struct vclock *next = vclockset_next(set, vclock);
		vclockset_remove(set, vclock);
		free(vclock);
		vclock = next;
	}
}

/**
 * Destroy xdir object and free memory.
 */
void
xdir_destroy(struct xdir *dir)
{
	/** Free vclock objects allocated in xdir_scan(). */
	vclockset_reset(&dir->index);
}

/**
 * Add a single log file to the index of all log files
 * in a given log directory.
 */
static inline int
xdir_index_file(struct xdir *dir, int64_t signature)
{
	/*
	 * Open xlog and parse vclock in its text header.
	 * The vclock stores the state of the log at the
	 * time it is created.
	 */
	struct xlog_cursor cursor;
	if (xdir_open_cursor(dir, signature, &cursor) < 0)
		return -1;
	struct xlog_meta *meta = &cursor.meta;

	/*
	 * All log files in a directory must satisfy Lamport's
	 * eventual order: events in each log file must be
	 * separable with consistent cuts, for example:
	 *
	 * log1: {1, 1, 0, 1}, log2: {1, 2, 0, 2} -- good
	 * log2: {1, 1, 0, 1}, log2: {2, 0, 2, 0} -- bad
	 */
	struct vclock *dup = vclockset_search(&dir->index, &meta->vclock);
	if (dup != NULL) {
		diag_set(XlogError, "%s: invalid xlog order", cursor.name);
		xlog_cursor_close(&cursor, false);
		return -1;
	}

	/*
	 * Append the clock describing the file to the
	 * directory index.
	 */
	struct vclock *vclock = (struct vclock *) malloc(sizeof(*vclock));
	if (vclock == NULL) {
		diag_set(OutOfMemory, sizeof(*vclock), "malloc", "vclock");
		xlog_cursor_close(&cursor, false);
		return -1;
	}

	vclock_copy(vclock, &meta->vclock);
	xlog_cursor_close(&cursor, false);
	vclockset_insert(&dir->index, vclock);
	return 0;
}

int
xdir_open_cursor(struct xdir *dir, int64_t signature,
		 struct xlog_cursor *cursor)
{
	const char *filename = xdir_format_filename(dir, signature, NONE);
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		diag_set(SystemError, "failed to open '%s' file", filename);
		return -1;
	}
	if (xlog_cursor_openfd(cursor, fd, filename) < 0) {
		close(fd);
		return -1;
	}
	struct xlog_meta *meta = &cursor->meta;
	if (strcmp(meta->filetype, dir->filetype) != 0) {
		xlog_cursor_close(cursor, false);
		diag_set(ClientError, ER_INVALID_XLOG_TYPE,
			 dir->filetype, meta->filetype);
		return -1;
	}
	if (!tt_uuid_is_nil(dir->instance_uuid) &&
	    !tt_uuid_is_equal(dir->instance_uuid, &meta->instance_uuid)) {
		xlog_cursor_close(cursor, false);
		diag_set(XlogError, "%s: invalid instance UUID", filename);
		return -1;
	}
	/*
	 * Check the match between log file name and contents:
	 * the sum of vector clock coordinates must be the same
	 * as the name of the file.
	 */
	int64_t signature_check = vclock_sum(&meta->vclock);
	if (signature_check != signature) {
		xlog_cursor_close(cursor, false);
		diag_set(XlogError, "%s: signature check failed", filename);
		return -1;
	}
	return 0;
}

static int
cmp_i64(const void *_a, const void *_b)
{
	const int64_t *a = (const int64_t *) _a, *b = (const int64_t *) _b;
	if (*a == *b)
		return 0;
	return (*a > *b) ? 1 : -1;
}

/**
 * Scan (or rescan) a directory with snapshot or write ahead logs.
 * Read all files matching a pattern from the directory -
 * the filename pattern is \d+.xlog
 * The name of the file is based on its vclock signature,
 * which is the sum of all elements in the vector clock recorded
 * when the file was created. Elements in the vector
 * reflect log sequence numbers of replicas in the asynchronous
 * replication set (see also _cluster system space and vclock.h
 * comments).
 *
 * This function tries to avoid re-reading a file if
 * it is already in the set of files "known" to the log
 * dir object. This is done to speed up local hot standby and
 * recovery_follow_local(), which periodically rescan the
 * directory to discover newly created logs.
 *
 * On error, this function throws an exception. If
 * dir->force_recovery is true, *some* errors are not
 * propagated up but only logged in the error log file.
 *
 * The list of errors ignored in force_recovery = true mode
 * includes:
 * - a file can not be opened
 * - some of the files have incorrect metadata (such files are
 *   skipped)
 *
 * The goal of force_recovery = true mode is partial recovery
 * from a damaged/incorrect data directory. It doesn't
 * silence conditions such as out of memory or lack of OS
 * resources.
 *
 * @return nothing.
 */
int
xdir_scan(struct xdir *dir)
{
	DIR *dh = opendir(dir->dirname);        /* log dir */
	int64_t *signatures = NULL;             /* log file names */
	size_t s_count = 0, s_capacity = 0;

	if (dh == NULL) {
		diag_set(SystemError, "error reading directory '%s'",
			  dir->dirname);
		return -1;
	}

	int rc = -1;
	struct vclock *vclock;
	struct dirent *dent;
	/*
	  A note regarding thread safety, readdir vs. readdir_r:

	  POSIX explicitly makes the following guarantee: "The
	  pointer returned by readdir() points to data which may
	  be overwritten by another call to readdir() on the same
	  directory stream. This data is not overwritten by another
	  call to readdir() on a different directory stream.

	  In practice, you don't have a problem with readdir(3)
	  because Android's bionic, Linux's glibc, and OS X and iOS'
	  libc all allocate per-DIR* buffers, and return pointers
	  into those; in Android's case, that buffer is currently about
	  8KiB. If future file systems mean that this becomes an actual
	  limitation, we can fix the C library and all your applications
	  will keep working.

	  See also
	  http://elliotth.blogspot.co.uk/2012/10/how-not-to-use-readdirr3.html
	*/
	while ((dent = readdir(dh)) != NULL) {
		char *ext = strchr(dent->d_name, '.');
		if (ext == NULL)
			continue;
		/*
		 * Compare the rest of the filename with
		 * dir->filename_ext.
		 */
		if (strcmp(ext, dir->filename_ext) != 0)
			continue;

		char *dot;
		long long signature = strtoll(dent->d_name, &dot, 10);
		if (ext != dot ||
		    signature == LLONG_MAX || signature == LLONG_MIN) {
			say_warn("can't parse `%s', skipping", dent->d_name);
			continue;
		}

		if (s_count == s_capacity) {
			s_capacity = s_capacity > 0 ? 2 * s_capacity : 16;
			size_t size = sizeof(*signatures) * s_capacity;
			signatures = (int64_t *) realloc(signatures, size);
			if (signatures == NULL) {
				diag_set(OutOfMemory,
					  size, "realloc", "signatures array");
				goto exit;
			}
		}
		signatures[s_count++] = signature;
	}
	/** Sort the list of files */
	if (s_count > 0)
		qsort(signatures, s_count, sizeof(*signatures), cmp_i64);
	/**
	 * Update the log dir index with the current state:
	 * remove files which no longer exist, add files which
	 * appeared since the last scan.
	 */
	vclock = vclockset_first(&dir->index);
	for (unsigned i = 0; i < s_count || vclock != NULL;) {
		int64_t s_old = vclock ? vclock_sum(vclock) : LLONG_MAX;
		int64_t s_new = i < s_count ? signatures[i] : LLONG_MAX;
		if (s_old < s_new) {
			/** Remove a deleted file from the index */
			struct vclock *next =
				vclockset_next(&dir->index, vclock);
			vclockset_remove(&dir->index, vclock);
			free(vclock);
			vclock = next;
		} else if (s_old > s_new) {
			/** Add a new file. */
			if (xdir_index_file(dir, s_new) != 0) {
				/*
				 * force_recovery must not affect OOM
				 */
				struct error *e = diag_last_error(&fiber()->diag);
				if (!dir->force_recovery ||
				    type_assignable(&type_OutOfMemory, e->type))
					goto exit;
				/** Skip a corrupted file */
				error_log(e);
			}
			i++;
		} else {
			assert(s_old == s_new && i < s_count &&
			       vclock != NULL);
			vclock = vclockset_next(&dir->index, vclock);
			i++;
		}
	}
	rc = 0;

exit:
	closedir(dh);
	free(signatures);
	return rc;
}

int
xdir_check(struct xdir *dir)
{
	DIR *dh = opendir(dir->dirname);        /* log dir */
	if (dh == NULL) {
		diag_set(SystemError, "error reading directory '%s'",
			  dir->dirname);
		return -1;
	}
	closedir(dh);
	return 0;
}

char *
xdir_format_filename(struct xdir *dir, int64_t signature,
		enum log_suffix suffix)
{
	static __thread char filename[PATH_MAX + 1];
	const char *suffix_str = (suffix == INPROGRESS ?
				  inprogress_suffix : "");
	snprintf(filename, PATH_MAX, "%s/%020lld%s%s",
		 dir->dirname, (long long) signature,
		 dir->filename_ext, suffix_str);
	return filename;
}

int
xdir_collect_garbage(struct xdir *dir, int64_t signature, bool use_coio)
{
	struct vclock *vclock;
	while ((vclock = vclockset_first(&dir->index)) != NULL &&
	       vclock_sum(vclock) < signature) {
		char *filename = xdir_format_filename(dir, vclock_sum(vclock),
						      NONE);
		say_info("removing %s", filename);
		int rc;
		if (use_coio)
			rc = coio_unlink(filename);
		else
			rc = unlink(filename);
		if (rc < 0 && errno != ENOENT) {
			say_syserror("error while removing %s", filename);
			diag_set(SystemError, "failed to unlink file '%s'",
				 filename);
			return -1;
		}
		vclockset_remove(&dir->index, vclock);
		free(vclock);
	}
	return 0;
}

/* }}} */


/* {{{ struct xlog */

int
xlog_rename(struct xlog *l)
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
		diag_set(SystemError, "failed to rename '%s' file",
				filename);
		return -1;
	}
	l->is_inprogress = false;
	return 0;
}

static int
xlog_init(struct xlog *xlog)
{
	memset(xlog, 0, sizeof(*xlog));
	xlog->sync_interval = SNAP_SYNC_INTERVAL;
	xlog->sync_time = ev_monotonic_time();
	xlog->is_autocommit = true;
	obuf_create(&xlog->obuf, &cord()->slabc, XLOG_TX_AUTOCOMMIT_THRESHOLD);
	obuf_create(&xlog->zbuf, &cord()->slabc, XLOG_TX_AUTOCOMMIT_THRESHOLD);
	xlog->zctx = ZSTD_createCCtx();
	if (xlog->zctx == NULL) {
		diag_set(ClientError, ER_COMPRESSION,
			 "failed to create context");
		return -1;
	}
	return 0;
}

void
xlog_clear(struct xlog *l)
{
	memset(l, 0, sizeof(*l));
	l->fd = -1;
}

static void
xlog_destroy(struct xlog *xlog)
{
	obuf_destroy(&xlog->obuf);
	obuf_destroy(&xlog->zbuf);
	ZSTD_freeCCtx(xlog->zctx);
	TRASH(xlog);
	xlog->fd = -1;
}

int
xlog_create(struct xlog *xlog, const char *name, int flags,
	    const struct xlog_meta *meta)
{
	char meta_buf[XLOG_META_LEN_MAX];
	int meta_len;

	/*
	 * Check that the file without .inprogress suffix doesn't exist.
	 */
	if (access(name, F_OK) == 0) {
		errno = EEXIST;
		diag_set(SystemError, "file '%s' already exists", name);
		goto err;
	}

	if (xlog_init(xlog) != 0)
		goto err;

	xlog->meta = *meta;
	xlog->is_inprogress = true;
	snprintf(xlog->filename, PATH_MAX, "%s%s", name, inprogress_suffix);

	flags |= O_RDWR | O_CREAT | O_EXCL;

	/*
	 * Open the <lsn>.<suffix>.inprogress file.
	 * If it exists, open will fail. Always open/create
	 * a file with .inprogress suffix: for snapshots,
	 * the rename is done when the snapshot is complete.
	 * Fox xlogs, we can rename only when we have written
	 * the log file header, otherwise replication relay
	 * may think that this is a corrupt file and stop
	 * replication.
	 */
	xlog->fd = open(xlog->filename, flags, 0644);
	if (xlog->fd < 0) {
		say_syserror("open, [%s]", name);
		diag_set(SystemError, "failed to create file '%s'", name);
		goto err_open;
	}

	/* Format metadata */
	meta_len = xlog_meta_format(&xlog->meta, meta_buf, sizeof(meta_buf));
	if (meta_len < 0)
		goto err_write;
	/* Formatted metadata must fit into meta_buf */
	assert(meta_len < (int)sizeof(meta_buf));

	/* Write metadata */
	if (fio_writen(xlog->fd, meta_buf, meta_len) < 0) {
		diag_set(SystemError, "%s: failed to write xlog meta", name);
		goto err_write;
	}

	xlog->offset = meta_len; /* first log starts after meta */
	return 0;
err_write:
	close(xlog->fd);
	unlink(xlog->filename); /* try to remove incomplete file */
err_open:
	xlog_destroy(xlog);
err:
	return -1;
}

int
xlog_open(struct xlog *xlog, const char *name)
{
	char magic[sizeof(log_magic_t)];
	char meta_buf[XLOG_META_LEN_MAX];
	const char *meta = meta_buf;
	int meta_len;
	int rc;

	if (xlog_init(xlog) != 0)
		goto err;

	strncpy(xlog->filename, name, PATH_MAX);
	xlog->fd = open(xlog->filename, O_RDWR);
	if (xlog->fd < 0) {
		say_syserror("open, [%s]", name);
		diag_set(SystemError, "failed to open file '%s'", name);
		goto err_open;
	}

	meta_len = fio_read(xlog->fd, meta_buf, sizeof(meta_buf));
	if (meta_len < 0) {
		diag_set(SystemError, "failed to read file '%s'",
			 xlog->filename);
		goto err_read;
	}

	rc = xlog_meta_parse(&xlog->meta, &meta, meta + meta_len);
	if (rc < 0)
		goto err_read;
	if (rc > 0) {
		diag_set(XlogError, "Unexpected end of file");
		goto err_read;
	}

	/*
	 * If the file has eof marker, reposition the file pointer so
	 * that the next write will overwrite it.
	 */
	xlog->offset = fio_lseek(xlog->fd, -(off_t)sizeof(magic), SEEK_END);
	if (xlog->offset < 0)
		goto no_eof;
	/* Use pread() so as not to change file pointer. */
	rc = fio_pread(xlog->fd, magic, sizeof(magic), xlog->offset);
	if (rc < 0) {
		diag_set(SystemError, "failed to read file '%s'",
			 xlog->filename);
		goto err_read;
	}
	if (rc != sizeof(magic) || load_u32(magic) != eof_marker) {
no_eof:
		xlog->offset = fio_lseek(xlog->fd, 0, SEEK_END);
		if (xlog->offset < 0) {
			diag_set(SystemError, "failed to seek file '%s'",
				 xlog->filename);
			goto err_read;
		}
	}
	return 0;
err_read:
	close(xlog->fd);
err_open:
	xlog_destroy(xlog);
err:
	return -1;
}

int
xdir_touch_xlog(struct xdir *dir, const struct vclock *vclock)
{
	char *filename;
	int64_t signature = vclock_sum(vclock);
	filename = xdir_format_filename(dir, signature, NONE);

	if (dir->type != SNAP) {
		assert(false);
		diag_set(SystemError, "Can't touch xlog '%s'", filename);
		return -1;
	}
	if (utime(filename, NULL) != 0) {
		diag_set(SystemError, "Can't update xlog timestamp: '%s'",
			 filename);
		return -1;
	}
	return 0;
}

/**
 * In case of error, writes a message to the error log
 * and sets errno.
 */
int
xdir_create_xlog(struct xdir *dir, struct xlog *xlog,
		 const struct vclock *vclock)
{
	char *filename;
	int64_t signature = vclock_sum(vclock);
	struct xlog_meta meta;
	assert(signature >= 0);
	assert(!tt_uuid_is_nil(dir->instance_uuid));

	/*
	* Check whether a file with this name already exists.
	* We don't overwrite existing files.
	*/
	filename = xdir_format_filename(dir, signature, NONE);

	/* Setup inherited values */
	snprintf(meta.filetype, sizeof(meta.filetype), "%s", dir->filetype);
	meta.instance_uuid = *dir->instance_uuid;
	vclock_copy(&meta.vclock, vclock);

	if (xlog_create(xlog, filename, dir->open_wflags, &meta) != 0)
		return -1;

	/* set sync interval from xdir settings */
	xlog->sync_interval = dir->sync_interval;
	/* free file cache if dir should be synced */
	xlog->free_cache = dir->sync_interval != 0 ? true: false;
	xlog->rate_limit = 0;

	/* Rename xlog file */
	if (dir->suffix != INPROGRESS && xlog_rename(xlog)) {
		int save_errno = errno;
		xlog_close(xlog, false);
		errno = save_errno;
		return -1;
	}

	return 0;
}

/**
 * Write a sequence of uncompressed xrow objects.
 *
 * @retval -1 error
 * @retval >= 0 the number of bytes written
 */
static off_t
xlog_tx_write_plain(struct xlog *log)
{
	/**
	 * We created an obuf savepoint at start of xlog_tx,
	 * now populate it with data.
	 */
	char *fixheader = (char *)log->obuf.iov[0].iov_base;
	*(log_magic_t *)fixheader = row_marker;
	char *data = fixheader + sizeof(log_magic_t);

	data = mp_encode_uint(data,
			      obuf_size(&log->obuf) - XLOG_FIXHEADER_SIZE);
	/* Encode crc32 for previous row */
	data = mp_encode_uint(data, 0);
	/* Encode crc32 for current row */
	uint32_t crc32c = 0;
	struct iovec *iov;
	size_t offset = XLOG_FIXHEADER_SIZE;
	for (iov = log->obuf.iov; iov->iov_len; ++iov) {
		crc32c = crc32_calc(crc32c,
				    (char *)iov->iov_base + offset,
				    iov->iov_len - offset);
		offset = 0;
	}
	data = mp_encode_uint(data, crc32c);
	/*
	 * Encode a padding, to ensure the resulting
	 * fixheader always has the same size.
	 */
	ssize_t padding = XLOG_FIXHEADER_SIZE - (data - fixheader);
	if (padding > 0) {
		data = mp_encode_strl(data, padding - 1);
		if (padding > 1) {
			memset(data, 0, padding - 1);
			data += padding - 1;
		}
	}

	ERROR_INJECT(ERRINJ_WAL_WRITE_DISK, {
		diag_set(ClientError, ER_INJECTION, "xlog write injection");
		return -1;
	});

	ssize_t written = fio_writevn(log->fd, log->obuf.iov, log->obuf.pos + 1);
	if (written < 0) {
		diag_set(SystemError, "failed to write to '%s' file",
			 log->filename);
		return -1;
	}
	return obuf_size(&log->obuf);
}

/**
 * Write a compressed block of xrow objects.
 * @retval -1  error
 * @retval >= 0 the number of bytes written
 */
static off_t
xlog_tx_write_zstd(struct xlog *log)
{
	char *fixheader = (char *)obuf_alloc(&log->zbuf,
					     XLOG_FIXHEADER_SIZE);

	uint32_t crc32c = 0;
	struct iovec *iov;
	/* 3 is compression level. */
	ZSTD_compressBegin(log->zctx, 3);
	size_t offset = XLOG_FIXHEADER_SIZE;
	for (iov = log->obuf.iov; iov->iov_len; ++iov) {
		/* Estimate max output buffer size. */
		size_t zmax_size = ZSTD_compressBound(iov->iov_len - offset);
		/* Allocate a destination buffer. */
		void *zdst = obuf_reserve(&log->zbuf, zmax_size);
		if (!zdst) {
			diag_set(OutOfMemory, zmax_size, "runtime arena",
				  "compression buffer");
			goto error;
		}
		size_t (*fcompress)(ZSTD_CCtx *, void *, size_t,
				    const void *, size_t);
		/*
		 * If it's the last iov or the last
		 * log has 0 bytes, end the stream.
		 */
		if (iov == log->obuf.iov + log->obuf.pos ||
		    !(iov + 1)->iov_len) {
			fcompress = ZSTD_compressEnd;
		} else {
			fcompress = ZSTD_compressContinue;
		}
		size_t zsize = fcompress(log->zctx, zdst, zmax_size,
					 (char *)iov->iov_base + offset,
					 iov->iov_len - offset);
		if (ZSTD_isError(zsize)) {
			diag_set(ClientError, ER_COMPRESSION,
				 ZSTD_getErrorName(zsize));
			goto error;
		}
		/* Advance output buffer to the end of compressed data. */
		obuf_alloc(&log->zbuf, zsize);
		/* Update crc32c */
		crc32c = crc32_calc(crc32c, (char *)zdst, zsize);
		/* Discount fixheader size for all iovs after first. */
		offset = 0;
	}

	*(log_magic_t *)fixheader = zrow_marker;
	char *data;
	data = fixheader + sizeof(log_magic_t);
	data = mp_encode_uint(data,
			      obuf_size(&log->zbuf) - XLOG_FIXHEADER_SIZE);
	/* Encode crc32 for previous row */
	data = mp_encode_uint(data, 0);
	/* Encode crc32 for current row */
	data = mp_encode_uint(data, crc32c);
	/* Encode padding */
	ssize_t padding;
	padding = XLOG_FIXHEADER_SIZE - (data - fixheader);
	if (padding > 0) {
		data = mp_encode_strl(data, padding - 1);
		if (padding > 1) {
			memset(data, 0, padding - 1);
			data += padding - 1;
		}
	}

	ERROR_INJECT(ERRINJ_WAL_WRITE_DISK, {
		diag_set(ClientError, ER_INJECTION, "xlog write injection");
		obuf_reset(&log->zbuf);
		goto error;
	});

	ssize_t written;
	written = fio_writevn(log->fd, log->zbuf.iov,
			      log->zbuf.pos + 1);
	if (written < 0) {
		diag_set(SystemError, "failed to write to '%s' file",
			 log->filename);
		goto error;
	}
	obuf_reset(&log->zbuf);
	return written;
error:
	obuf_reset(&log->zbuf);
	return -1;
}

/* file syncing and posix_fadvise() should be rounded by a page boundary */
#define SYNC_MASK		(4096 - 1)
#define SYNC_ROUND_DOWN(size)	((size) & ~(4096 - 1))
#define SYNC_ROUND_UP(size)	(SYNC_ROUND_DOWN(size + SYNC_MASK))

/**
 * Writes xlog batch to file
 */
static ssize_t
xlog_tx_write(struct xlog *log)
{
	if (obuf_size(&log->obuf) == XLOG_FIXHEADER_SIZE)
		return 0;
	ssize_t written;

	if (obuf_size(&log->obuf) >= XLOG_TX_COMPRESS_THRESHOLD) {
		written = xlog_tx_write_zstd(log);
	} else {
		written = xlog_tx_write_plain(log);
	}
	ERROR_INJECT(ERRINJ_WAL_WRITE, {
		diag_set(ClientError, ER_INJECTION, "xlog write injection");
		written = -1;
	});

	obuf_reset(&log->obuf);
	/*
	 * Simplify recovery after a temporary write failure:
	 * truncate the file to the best known good write
	 * position.
	 */
	if (written < 0) {
		if (lseek(log->fd, log->offset, SEEK_SET) < 0 ||
		    ftruncate(log->fd, log->offset) != 0)
			panic_syserror("failed to truncate xlog after write error");
		return -1;
	}
	log->offset += written;
	log->rows += log->tx_rows;
	log->tx_rows = 0;
	if ((log->sync_interval && log->offset >=
	    (off_t)(log->synced_size + log->sync_interval)) ||
	    (log->rate_limit && log->offset >=
	    (off_t)(log->synced_size + log->rate_limit))) {
		off_t sync_from = SYNC_ROUND_DOWN(log->synced_size);
		size_t sync_len = SYNC_ROUND_UP(log->offset) -
				  sync_from;
		if (log->rate_limit > 0) {
			double throttle_time;
			throttle_time = (double)sync_len / log->rate_limit -
					(ev_monotonic_time() - log->sync_time);
			if (throttle_time > 0)
				fiber_sleep(throttle_time);
		}
		/** sync data from cache to disk */
#ifdef HAVE_SYNC_FILE_RANGE
		sync_file_range(log->fd, sync_from, sync_len,
				SYNC_FILE_RANGE_WAIT_BEFORE |
				SYNC_FILE_RANGE_WRITE |
				SYNC_FILE_RANGE_WAIT_AFTER);
#else
		fdatasync(log->fd);
#endif /* HAVE_SYNC_FILE_RANGE */
		log->sync_time = ev_monotonic_time();
		if (log->free_cache) {
#ifdef HAVE_POSIX_FADVISE
			/** free page cache */
			if (posix_fadvise(log->fd, sync_from, sync_len,
					  POSIX_FADV_DONTNEED) != 0) {
				say_syserror("posix_fadvise, fd=%i", log->fd);
			}
#else
			(void) sync_from;
			(void) sync_len;
#endif /* HAVE_POSIX_FADVISE */
		}
		log->synced_size = log->offset;
	}
	return written;
}

/*
 * Add a row to a log and possibly flush the log.
 *
 * @retval  -1 error, check diag.
 * @retval >=0 the number of bytes written to buffer.
 */
ssize_t
xlog_write_row(struct xlog *log, const struct xrow_header *packet)
{
	/*
	 * Automatically reserve space for a fixheader when adding
	 * the first row in * a log. The fixheader is populated
	 * at write. @sa xlog_tx_write().
	 */
	if (obuf_size(&log->obuf) == 0) {
		if (!obuf_alloc(&log->obuf, XLOG_FIXHEADER_SIZE)) {
			diag_set(OutOfMemory, XLOG_FIXHEADER_SIZE,
				  "runtime arena", "xlog tx output buffer");
			return -1;
		}
	}

	struct obuf_svp svp = obuf_create_svp(&log->obuf);
	size_t page_offset = obuf_size(&log->obuf);
	/** encode row into iovec */
	struct iovec iov[XROW_IOVMAX];
	/** don't write sync to the disk */
	int iovcnt = xrow_header_encode(packet, 0, iov, 0);
	if (iovcnt < 0) {
		obuf_rollback_to_svp(&log->obuf, &svp);
		return -1;
	}
	for (int i = 0; i < iovcnt; ++i) {
		struct errinj *inj = errinj(ERRINJ_WAL_WRITE_PARTIAL,
					    ERRINJ_INT);
		if (inj != NULL && inj->iparam >= 0 &&
		    obuf_size(&log->obuf) > (size_t)inj->iparam) {
			diag_set(ClientError, ER_INJECTION,
				 "xlog write injection");
			obuf_rollback_to_svp(&log->obuf, &svp);
			return -1;
		};
		if (obuf_dup(&log->obuf, iov[i].iov_base, iov[i].iov_len) <
		    iov[i].iov_len) {
			diag_set(OutOfMemory, XLOG_FIXHEADER_SIZE,
				  "runtime arena", "xlog tx output buffer");
			obuf_rollback_to_svp(&log->obuf, &svp);
			return -1;
		}
	}
	assert(iovcnt <= XROW_IOVMAX);
	log->tx_rows++;

	size_t row_size = obuf_size(&log->obuf) - page_offset;
	if (log->is_autocommit &&
	    obuf_size(&log->obuf) >= XLOG_TX_AUTOCOMMIT_THRESHOLD &&
	    xlog_tx_write(log) < 0)
		return -1;

	return row_size;
}

/**
 * Begin a multi-statement xlog transaction. All xrow objects
 * of a single transaction share the same header and checksum
 * and are normally written at once.
 */
void
xlog_tx_begin(struct xlog *log)
{
	log->is_autocommit = false;
}

/*
 * End a non-interruptible batch of rows, thus enable flushes of
 * a transaction at any time, on threshold. If the buffer is big
 * enough already, flush it at once here.
 *
 * @retval -1  error
 * @retval >= 0 the number of bytes written to disk
 */
ssize_t
xlog_tx_commit(struct xlog *log)
{
	log->is_autocommit = true;
	if (obuf_size(&log->obuf) >= XLOG_TX_AUTOCOMMIT_THRESHOLD) {
		return xlog_tx_write(log);
	}
	return 0;
}

/*
 * Rollback a batch of buffered rows without writing to file
 */
void
xlog_tx_rollback(struct xlog *log)
{
	log->is_autocommit = true;
	log->tx_rows = 0;
	obuf_reset(&log->obuf);
}

/**
 * Flush any outstanding xlog_tx transactions at the end of
 * a WAL write batch.
 */
ssize_t
xlog_flush(struct xlog *log)
{
	assert(log->is_autocommit);
	if (log->obuf.used == 0)
		return 0;
	return xlog_tx_write(log);
}

static int
sync_cb(eio_req *req)
{
	int fd = (intptr_t) req->data;
	if (req->result) {
		errno = req->errorno;
		say_syserror("%s: fsync() failed",
			     fio_filename(fd));
		errno = 0;
	}
	close(fd);
	return 0;
}

int
xlog_sync(struct xlog *l)
{
	if (l->sync_is_async) {
		int fd = dup(l->fd);
		if (fd == -1) {
			say_syserror("%s: dup() failed", l->filename);
			return -1;
		}
		eio_fsync(fd, 0, sync_cb, (void *) (intptr_t) fd);
	} else if (fsync(l->fd) < 0) {
		say_syserror("%s: fsync failed", l->filename);
		return -1;
	}
	return 0;
}

static int
xlog_write_eof(struct xlog *l)
{
	ERROR_INJECT(ERRINJ_WAL_WRITE_EOF, {
		diag_set(ClientError, ER_INJECTION, "xlog write injection");
		return -1;
	});
	if (fio_writen(l->fd, &eof_marker, sizeof(eof_marker)) < 0) {
		diag_set(SystemError, "write() failed");
		return -1;
	}
	return 0;
}

int
xlog_close(struct xlog *l, bool reuse_fd)
{
	int rc = xlog_write_eof(l);
	if (rc < 0)
		say_error("%s: failed to write EOF marker: %s", l->filename,
			  diag_last_error(diag_get())->errmsg);

	/*
	 * Sync the file before closing, since
	 * otherwise we can end up with a partially
	 * written file in case of a crash.
	 * We sync even if file open O_SYNC, simplify code for low cost
	 */
	xlog_sync(l);

	if (!reuse_fd) {
		rc = close(l->fd);
		if (rc < 0)
			say_syserror("%s: close() failed", l->filename);
	}

	xlog_destroy(l);
	return rc;
}

/**
 * Free xlog memory and destroy it cleanly, without side
 * effects (for use in the atfork handler).
 */
void
xlog_atfork(struct xlog *xlog)
{
	/*
	 * Close the file descriptor STDIO buffer does not
	 * make its way into the respective file in
	 * fclose().
	 */
	close(xlog->fd);
	xlog->fd = -1;
}

/* }}} */

/* {{{ struct xlog_cursor */

#define XLOG_READ_AHEAD		(1 << 14)

/**
 * Ensure that at least count bytes are in read buffer
 *
 * @retval 0 at least count bytes are in read buf
 * @retval 1 if eof
 * @retval -1 if error
 */
static int
xlog_cursor_ensure(struct xlog_cursor *cursor, size_t count)
{
	if (ibuf_used(&cursor->rbuf) >= count)
		return 0;
	/* in-memory mode */
	if (cursor->fd < 0)
		return 1;

	size_t to_load = count - ibuf_used(&cursor->rbuf);
	to_load += XLOG_READ_AHEAD;

	void *dst = ibuf_reserve(&cursor->rbuf, to_load);
	if (dst == NULL) {
		diag_set(OutOfMemory, to_load, "runtime",
			 "xlog cursor read buffer");
		return -1;
	}
	ssize_t readen;
	readen = fio_pread(cursor->fd, dst, to_load,
			   cursor->read_offset);
	struct errinj *inj = errinj(ERRINJ_XLOG_READ, ERRINJ_INT);
	if (inj != NULL && inj->iparam >= 0 &&
	    inj->iparam < cursor->read_offset) {
		readen = -1;
		errno = EIO;
	};
	if (readen < 0) {
		diag_set(SystemError, "failed to read '%s' file",
			 cursor->name);
		return -1;
	}
	/* ibuf_reserve() has been called above, ibuf_alloc() must not fail */
	assert((size_t)readen <= to_load);
	ibuf_alloc(&cursor->rbuf, readen);
	cursor->read_offset += readen;
	return ibuf_used(&cursor->rbuf) >= count ? 0: 1;
}

/**
 * Decompress zstd-compressed buf into cursor row block
 *
 * @retval -1 error, check diag
 * @retval  0 data fully decompressed
 * @retval  1 need more bytes in the output buffer
 */
static int
xlog_cursor_decompress(char **rows, char *rows_end, const char **data,
		       const char *data_end, ZSTD_DStream *zdctx)
{
	ZSTD_inBuffer input = {*data, (size_t)(data_end - *data), 0};
	ZSTD_outBuffer output = {*rows, (size_t)(rows_end - *rows), 0};

	while (input.pos < input.size && output.pos < output.size) {
		size_t rc = ZSTD_decompressStream(zdctx, &output, &input);
		if (ZSTD_isError(rc)) {
			diag_set(ClientError, ER_DECOMPRESSION,
				 ZSTD_getErrorName(rc));
			return -1;
		}
		assert(output.pos <= (size_t)(rows_end - *rows));
		*rows = (char *)output.dst + output.pos;
		*data = (char *)input.src + input.pos;
	}
	return input.pos == input.size ? 0: 1;
}

/**
 * xlog fixheader struct
 */
struct xlog_fixheader {
	/**
	 * xlog tx magic, row_marker for plain xrows
	 * or zrow_marker for compressed.
	 */
	log_magic_t magic;
	/**
	 * crc32 for the previous xlog tx, not used now
	 */
	uint32_t crc32p;
	/**
	 * crc32 for current xlog tx
	 */
	uint32_t crc32c;
	/**
	 * xlog tx data length excluding fixheader
	 */
	uint32_t len;
};

/**
 * Decode xlog tx header, set up magic, crc32c and len
 *
 * @retval 0 for success
 * @retval -1 for error
 * @retval count of bytes left to parse header
 */
static ssize_t
xlog_fixheader_decode(struct xlog_fixheader *fixheader,
		      const char **data, const char *data_end)
{
	if (data_end - *data < (ptrdiff_t)XLOG_FIXHEADER_SIZE)
		return XLOG_FIXHEADER_SIZE - (data_end - *data);
	const char *pos = *data;
	const char *end = pos + XLOG_FIXHEADER_SIZE;

	/* Decode magic */
	fixheader->magic = load_u32(pos);
	if (fixheader->magic != row_marker &&
	    fixheader->magic != zrow_marker) {
		diag_set(XlogError, "invalid magic: 0x%x", fixheader->magic);
		return -1;
	}
	pos += sizeof(fixheader->magic);

	/* Read length */
	const char *val = pos;
	if (pos >= end || mp_check(&pos, end) != 0 ||
	    mp_typeof(*val) != MP_UINT) {
		diag_set(XlogError, "broken fixheader length");
		return -1;
	}
	fixheader->len = mp_decode_uint(&val);
	assert(val == pos);
	if (fixheader->len > IPROTO_BODY_LEN_MAX) {
		diag_set(XlogError, "too large fixheader length");
		return -1;
	}

	/* Read previous crc32 */
	if (pos >= end || mp_check(&pos, end) != 0 ||
	    mp_typeof(*val) != MP_UINT) {
		diag_set(XlogError, "broken fixheader crc32p");
		return -1;
	}
	fixheader->crc32p = mp_decode_uint(&val);
	assert(val == pos);

	/* Read current crc32 */
	if (pos >= end || mp_check(&pos, end) != 0 ||
	    mp_typeof(*val) != MP_UINT) {
		diag_set(XlogError, "broken fixheader crc32c");
		return -1;
	}
	fixheader->crc32c = mp_decode_uint(&val);
	assert(val == pos);

	/* Check and skip padding if any */
	if (pos < end && (mp_check(&pos, end) != 0 || pos != end)) {
		diag_set(XlogError, "broken fixheader padding");
		return -1;
	}

	assert(pos == end);
	*data = end;
	return 0;
}

int
xlog_tx_decode(const char *data, const char *data_end,
	       char *rows, char *rows_end, ZSTD_DStream *zdctx)
{
	/* Decode fixheader */
	struct xlog_fixheader fixheader;
	if (xlog_fixheader_decode(&fixheader, &data, data_end) != 0)
		return -1;

	/* Check that buffer has enough bytes */
	if (data + fixheader.len != data_end) {
		diag_set(XlogError, "invalid compressed length: "
			  "expected %zd, got %u",
			  data_end - data, fixheader.len);
		return -1;
	}

	ERROR_INJECT(ERRINJ_XLOG_GARBAGE, {
		*((char *)data + fixheader.len / 2) = ~*((char *)data + fixheader.len / 2);
	});

	/* Validate checksum */
	if (crc32_calc(0, data, fixheader.len) != fixheader.crc32c) {
		diag_set(XlogError, "tx checksum mismatch");
		return -1;
	}

	/* Copy uncompressed rows */
	if (fixheader.magic == row_marker) {
		if (rows_end - rows != (ptrdiff_t)fixheader.len) {
			diag_set(XlogError, "invalid unpacked length: "
				  "expected %zd, got %u",
				  rows_end - data, fixheader.len);
			return -1;
		}
		memcpy(rows, data, fixheader.len);
		return 0;
	}

	/* Decompress zstd rows */
	assert(fixheader.magic == zrow_marker);
	ZSTD_initDStream(zdctx);
	int rc = xlog_cursor_decompress(&rows, rows_end, &data, data_end,
					zdctx);
	if (rc < 0) {
		return -1;
	} else if (rc > 0) {
		diag_set(XlogError, "invalid decompressed length: "
			  "expected %zd, got %zd", rows_end - data,
			   rows_end - data + XLOG_TX_AUTOCOMMIT_THRESHOLD);
		return -1;
	}

	assert(data == data_end);
	return 0;
}

/**
 * @retval -1 error
 * @retval 0 success
 * @retval >0 how many bytes we will have for continue
 */
ssize_t
xlog_tx_cursor_create(struct xlog_tx_cursor *tx_cursor,
		      const char **data, const char *data_end,
		      ZSTD_DStream *zdctx)
{
	const char *rpos = *data;
	struct xlog_fixheader fixheader;
	ssize_t to_load;
	to_load = xlog_fixheader_decode(&fixheader, &rpos, data_end);
	if (to_load != 0)
		return to_load;

	/* Check that buffer has enough bytes */
	if ((data_end - rpos) < (ptrdiff_t)fixheader.len)
		return fixheader.len - (data_end - rpos);

	ERROR_INJECT(ERRINJ_XLOG_GARBAGE, {
		*((char *)rpos + fixheader.len / 2) = ~*((char *)rpos + fixheader.len / 2);
	});

	/* Validate checksum */
	if (crc32_calc(0, rpos, fixheader.len) != fixheader.crc32c) {
		diag_set(XlogError, "tx checksum mismatch");
		return -1;
	}
	data_end = rpos + fixheader.len;

	ibuf_create(&tx_cursor->rows, &cord()->slabc,
		    XLOG_TX_AUTOCOMMIT_THRESHOLD);
	if (fixheader.magic == row_marker) {
		void *dst = ibuf_alloc(&tx_cursor->rows, fixheader.len);
		if (dst == NULL) {
			diag_set(OutOfMemory, fixheader.len,
				 "runtime", "xlog rows buffer");
			ibuf_destroy(&tx_cursor->rows);
			return -1;
		}
		memcpy(dst, rpos, fixheader.len);
		*data = (char *)rpos + fixheader.len;
		assert(*data <= data_end);
		tx_cursor->size = ibuf_used(&tx_cursor->rows);
		return 0;
	};

	assert(fixheader.magic == zrow_marker);
	ZSTD_initDStream(zdctx);
	int rc;
	do {
		if (ibuf_reserve(&tx_cursor->rows,
				 XLOG_TX_AUTOCOMMIT_THRESHOLD) == NULL) {
			diag_set(OutOfMemory, XLOG_TX_AUTOCOMMIT_THRESHOLD,
				  "runtime", "xlog output buffer");
			ibuf_destroy(&tx_cursor->rows);
			return -1;
		}
	} while ((rc = xlog_cursor_decompress(&tx_cursor->rows.wpos,
					      tx_cursor->rows.end, &rpos,
					      data_end, zdctx)) == 1);
	if (rc != 0)
		return -1;

	*data = rpos;
	assert(*data <= data_end);
	tx_cursor->size = ibuf_used(&tx_cursor->rows);
	return 0;
}

int
xlog_tx_cursor_next_row(struct xlog_tx_cursor *tx_cursor,
		        struct xrow_header *xrow)
{
	if (ibuf_used(&tx_cursor->rows) == 0)
		return 1;
	/* Return row from xlog tx buffer */
	int rc = xrow_header_decode(xrow,
				    (const char **)&tx_cursor->rows.rpos,
				    (const char *)tx_cursor->rows.wpos);
	if (rc != 0) {
		diag_set(XlogError, "can't parse row");
		/* Discard remaining row data */
		ibuf_reset(&tx_cursor->rows);
		return -1;
	}

	return 0;
}

int
xlog_tx_cursor_destroy(struct xlog_tx_cursor *tx_cursor)
{
	ibuf_destroy(&tx_cursor->rows);
	return 0;
}

/**
 * Find a next xlog tx magic
 */
int
xlog_cursor_find_tx_magic(struct xlog_cursor *i)
{
	assert(xlog_cursor_is_open(i));
	log_magic_t magic;
	do {
		/*
		 * Read one extra byte to start searching from the next
		 * byte.
		 */
		int rc = xlog_cursor_ensure(i, sizeof(log_magic_t) + 1);
		if (rc < 0)
			return -1;
		if (rc == 1)
			return 1;

		++i->rbuf.rpos;
		assert(i->rbuf.rpos + sizeof(log_magic_t) <= i->rbuf.wpos);
		magic = load_u32(i->rbuf.rpos);
	} while (magic != row_marker && magic != zrow_marker);

	return 0;
}

int
xlog_cursor_next_tx(struct xlog_cursor *i)
{
	int rc;
	assert(xlog_cursor_is_open(i));

	/* load at least magic to check eof */
	rc = xlog_cursor_ensure(i, sizeof(log_magic_t));
	if (rc < 0)
		return -1;
	if (rc > 0)
		return 1;
	if (load_u32(i->rbuf.rpos) == eof_marker) {
		/* eof marker found */
		goto eof_found;
	}

	ssize_t to_load;
	while ((to_load = xlog_tx_cursor_create(&i->tx_cursor,
						(const char **)&i->rbuf.rpos,
						i->rbuf.wpos, i->zdctx)) > 0) {
		/* not enough data in read buffer */
		int rc = xlog_cursor_ensure(i, ibuf_used(&i->rbuf) + to_load);
		if (rc < 0)
			return -1;
		if (rc > 0)
			return 1;
	}
	if (to_load < 0)
		return -1;

	i->state = XLOG_CURSOR_TX;
	return 0;
eof_found:
	/*
	 * A eof marker is read, check that there is no
	 * more data in the file.
	 */
	rc = xlog_cursor_ensure(i, sizeof(log_magic_t) + sizeof(char));

	if (rc < 0)
		return -1;
	if (rc == 0) {
		diag_set(XlogError, "%s: has some data after "
			  "eof marker at %lld", i->name,
			  xlog_cursor_pos(i));
		return -1;
	}
	i->state = XLOG_CURSOR_EOF;
	return 1;
}

int
xlog_cursor_next_row(struct xlog_cursor *cursor, struct xrow_header *xrow)
{
	assert(xlog_cursor_is_open(cursor));
	if (cursor->state != XLOG_CURSOR_TX)
		return 1;
	int rc = xlog_tx_cursor_next_row(&cursor->tx_cursor, xrow);
	if (rc != 0) {
		cursor->state = XLOG_CURSOR_ACTIVE;
		xlog_tx_cursor_destroy(&cursor->tx_cursor);
	}
	return rc;
}

int
xlog_cursor_next(struct xlog_cursor *cursor,
		 struct xrow_header *xrow, bool force_recovery)
{
	assert(xlog_cursor_is_open(cursor));
	while (true) {
		int rc;
		rc = xlog_cursor_next_row(cursor, xrow);
		if (rc == 0)
			break;
		if (rc < 0) {
			struct error *e = diag_last_error(diag_get());
			if (!force_recovery ||
			    e->type != &type_XlogError)
				return -1;
			say_error("can't decode row: %s", e->errmsg);
		}
		while ((rc = xlog_cursor_next_tx(cursor)) < 0) {
			struct error *e = diag_last_error(diag_get());
			if (!force_recovery ||
			    e->type != &type_XlogError)
				return -1;
			say_error("can't open tx: %s", e->errmsg);
			if ((rc = xlog_cursor_find_tx_magic(cursor)) < 0)
				return -1;
			if (rc > 0)
				break;
		}
		if (rc == 1)
			return 1;
	}
	return 0;
}

int
xlog_cursor_openfd(struct xlog_cursor *i, int fd, const char *name)
{
	memset(i, 0, sizeof(*i));
	i->fd = fd;
	ibuf_create(&i->rbuf, &cord()->slabc,
		    XLOG_TX_AUTOCOMMIT_THRESHOLD << 1);

	ssize_t rc;
	/*
	 * we can have eof here, but this is no error,
	 * because we don't know exact meta size
	 */
	rc = xlog_cursor_ensure(i, XLOG_META_LEN_MAX);
	if (rc == -1)
		goto error;
	rc = xlog_meta_parse(&i->meta,
			     (const char **)&i->rbuf.rpos,
			     (const char *)i->rbuf.wpos);
	if (rc == -1)
		goto error;
	if (rc > 0) {
		diag_set(XlogError, "Unexpected end of file, run with 'force_recovery = true'");
		goto error;
	}
	snprintf(i->name, PATH_MAX, "%s", name);
	i->zdctx = ZSTD_createDStream();
	if (i->zdctx == NULL) {
		diag_set(ClientError, ER_DECOMPRESSION,
			 "failed to create context");
		goto error;
	}
	i->state = XLOG_CURSOR_ACTIVE;
	return 0;
error:
	ibuf_destroy(&i->rbuf);
	return -1;
}

int
xlog_cursor_open(struct xlog_cursor *i, const char *name)
{
	int fd = open(name, O_RDONLY);
	if (fd < 0) {
		diag_set(SystemError, "failed to open '%s' file", name);
		return -1;
	}
	int rc = xlog_cursor_openfd(i, fd, name);
	if (rc < 0) {
		close(fd);
		return -1;
	}
	return 0;
}

int
xlog_cursor_openmem(struct xlog_cursor *i, const char *data, size_t size,
		    const char *name)
{
	memset(i, 0, sizeof(*i));
	i->fd = -1;
	ibuf_create(&i->rbuf, &cord()->slabc,
		    XLOG_TX_AUTOCOMMIT_THRESHOLD << 1);

	void *dst = ibuf_alloc(&i->rbuf, size);
	if (dst == NULL) {
		diag_set(OutOfMemory, size, "runtime",
			  "xlog cursor read buffer");
		goto error;
	}
	memcpy(dst, data, size);
	i->read_offset = size;
	int rc;
	rc = xlog_meta_parse(&i->meta,
			     (const char **)&i->rbuf.rpos,
			     (const char *)i->rbuf.wpos);
	if (rc < 0)
		goto error;
	if (rc > 0) {
		diag_set(XlogError, "Unexpected end of file");
		goto error;
	}
	snprintf(i->name, PATH_MAX, "%s", name);
	i->zdctx = ZSTD_createDStream();
	if (i->zdctx == NULL) {
		diag_set(ClientError, ER_DECOMPRESSION,
			 "failed to create context");
		goto error;
	}
	i->state = XLOG_CURSOR_ACTIVE;
	return 0;
error:
	ibuf_destroy(&i->rbuf);
	return -1;
}

void
xlog_cursor_close(struct xlog_cursor *i, bool reuse_fd)
{
	assert(xlog_cursor_is_open(i));
	if (i->fd >= 0 && !reuse_fd)
		close(i->fd);
	ibuf_destroy(&i->rbuf);
	if (i->state == XLOG_CURSOR_TX)
		xlog_tx_cursor_destroy(&i->tx_cursor);
	ZSTD_freeDStream(i->zdctx);
	i->state = (i->state == XLOG_CURSOR_EOF ?
		    XLOG_CURSOR_EOF_CLOSED : XLOG_CURSOR_CLOSED);
	/*
	 * Do not trash the cursor object since the caller might
	 * still want to access its state and/or meta information.
	 */
}

/* }}} */
