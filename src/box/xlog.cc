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
#include "crc32.h"
#include "fio.h"
#include "third_party/tarantool_eio.h"
#include <msgpuck.h>
#include "scoped_guard.h"

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

const struct type type_XlogError = make_type("XlogError", &type_Exception);
XlogError::XlogError(const char *file, unsigned line,
		     const char *format, ...)
	:Exception(&type_XlogError, file, line)
{
	va_list ap;
	va_start(ap, format);
	error_vformat_msg(this, format, ap);
	va_end(ap);
}

XlogError::XlogError(const struct type *type, const char *file, unsigned line,
		     const char *format, ...)
	:Exception(type, file, line)
{
	va_list ap;
	va_start(ap, format);
	error_vformat_msg(this, format, ap);
	va_end(ap);
}

const struct type type_XlogGapError =
	make_type("XlogGapError", &type_XlogError);

XlogGapError::XlogGapError(const char *file, unsigned line,
			   const struct vclock *from,
			   const struct vclock *to)
	:XlogError(&type_XlogGapError, file, line, "")
{
	char *s_from = vclock_to_string(from);
	char *s_to = vclock_to_string(to);
	snprintf(errmsg, sizeof(errmsg),
		 "Missing .xlog file between LSN %lld %s and %lld %s",
		 (long long) vclock_sum(from), s_from ? s_from : "",
		 (long long) vclock_sum(to), s_to ? s_to : "");
	free(s_from);
	free(s_to);
}

/* {{{ struct xlog_meta */

enum {
	/*
	 * The maximum length of xlog meta
	 *
	 * @sa xlog_meta_parse()
	 */
	XLOG_META_LEN_MAX = 1024 + VCLOCK_STR_LEN_MAX
};

#define SERVER_UUID_KEY "Server"
#define VCLOCK_KEY "VClock"

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
 * @retval -1 - error
 * @sa snprintf()
 */
static int
xlog_meta_format(const struct xlog_meta *meta, char *buf, int size)
{
	char *vstr = vclock_to_string(&meta->vclock);
	char *server_uuid = tt_uuid_str(&meta->server_uuid);
	int total = snprintf(buf, size, "%s\n%s\n" SERVER_UUID_KEY ": "
		"%s\n" VCLOCK_KEY ": %s\n\n",
		 meta->filetype, v13, server_uuid, vstr);
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
		tnt_error(XlogError, "failed to parse xlog type string");
		return -1;
	}
	memcpy(meta->filetype, pos, eol - pos);
	meta->filetype[eol - pos] = '\0';
	pos = eol + 1;
	assert(pos <= end);

	/*
	 * Parse version string, i.e. "0.12" or "0.13"
	 */
	eol = (const char *)memchr(pos, '\n', end - pos);
	if (eol == end || (eol - pos) >= (ptrdiff_t) sizeof(meta->version)) {
		tnt_error(XlogError, "failed to parse xlog version string");
		return -1;
	}
	memcpy(meta->version, pos, eol - pos);
	meta->version[eol - pos] = '\0';
	pos = eol + 1;
	assert(pos <= end);
	if (strncmp(meta->version, v12, sizeof(v12)) != 0 &&
	    strncmp(meta->version, v13, sizeof(v13)) != 0) {
		tnt_error(XlogError,
			  "unsupported file format version %s",
			  meta->version);
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
			tnt_error(XlogError, "can't extract meta value");
			return -1;
		}
		const char *val = key_end + 1;
		/* Skip space after colon */
		while (*val == ' ' || *val == '\t')
			++val;
		const char *val_end = eol;
		assert(val <= val_end);
		pos = eol + 1;

		if (memcmp(key, SERVER_UUID_KEY, key_end - key) == 0) {
			/*
			 * Server: <uuid>
			 */
			if (val_end - val != UUID_STR_LEN) {
				tnt_error(XlogError, "can't parse node UUID");
				return -1;
			}
			char uuid[UUID_STR_LEN + 1];
			memcpy(uuid, val, UUID_STR_LEN);
			uuid[UUID_STR_LEN] = '\0';
			if (tt_uuid_from_string(uuid, &meta->server_uuid) != 0) {
				tnt_error(XlogError, "can't parse node UUID");
				return -1;
			}
		} else if (memcmp(key, VCLOCK_KEY, key_end - key) == 0){
			/*
			 * VClock: <vclock>
			 */
			if (val_end - val > VCLOCK_STR_LEN_MAX) {
				tnt_error(XlogError, "can't parse vclock");
				return -1;
			}
			char vclock[VCLOCK_STR_LEN_MAX + 1];
			memcpy(vclock, val, val_end - val);
			vclock[val_end - val] = '\0';
			size_t off = vclock_from_string(&meta->vclock, vclock);
			if (off != 0) {
				tnt_error(XlogError, "invalid vclock at "
					  "offset %zd", off);
				return -1;
			}
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
	    enum xdir_type type, const tt_uuid *server_uuid)
{
	memset(dir, 0, sizeof(*dir));
	vclockset_new(&dir->index);
	/* Default mode. */
	dir->mode = 0660;
	dir->server_uuid = server_uuid;
	snprintf(dir->dirname, PATH_MAX, "%s", dirname);
	dir->open_wflags = O_RDWR | O_CREAT | O_EXCL;
	if (type == SNAP) {
		dir->filetype = "SNAP";
		dir->filename_ext = ".snap";
		dir->panic_if_error = true;
		dir->suffix = INPROGRESS;
		dir->sync_interval = SNAP_SYNC_INTERVAL;
	} else {
		dir->sync_is_async = true;
		dir->filetype = "XLOG";
		dir->filename_ext = ".xlog";
		dir->suffix = NONE;
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
		tnt_error(XlogError, "%s: invalid xlog order", cursor.name);
		xlog_cursor_close(&cursor);
		return -1;
	}

	/*
	 * Append the clock describing the file to the
	 * directory index.
	 */
	struct vclock *vclock = (struct vclock *) malloc(sizeof(*vclock));
	if (vclock == NULL) {
		tnt_error(OutOfMemory, sizeof(*vclock), "malloc", "vclock");
		xlog_cursor_close(&cursor);
		return -1;
	}

	vclock_copy(vclock, &meta->vclock);
	xlog_cursor_close(&cursor);
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
		xlog_cursor_close(cursor);
		tnt_error(XlogError, "%s: unknown filetype", filename);
		return -1;
	}
	if (!tt_uuid_is_nil(dir->server_uuid) &&
	    !tt_uuid_is_equal(dir->server_uuid, &meta->server_uuid)) {
		xlog_cursor_close(cursor);
		tnt_error(XlogError, "%s: invalid server UUID", filename);
		return -1;
	}
	/*
	 * Check the match between log file name and contents:
	 * the sum of vector clock coordinates must be the same
	 * as the name of the file.
	 */
	int64_t signature_check = vclock_sum(&meta->vclock);
	if (signature_check != signature) {
		xlog_cursor_close(cursor);
		tnt_error(XlogError, "%s: signature check failed", filename);
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
 * reflect log sequence numbers of servers in the asynchronous
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
 * dir->panic_if_error is false, *some* errors are not
 * propagated up but only logged in the error log file.
 *
 * The list of errors ignored in panic_if_error = false mode
 * includes:
 * - a file can not be opened
 * - some of the files have incorrect metadata (such files are
 *   skipped)
 *
 * The goal of panic_if_error = false mode is partial recovery
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
		tnt_error(SystemError, "error reading directory '%s'",
			  dir->dirname);
		return -1;
	}

	auto dir_guard = make_scoped_guard([&]{
		closedir(dh);
		free(signatures);
	});

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
				tnt_error(OutOfMemory,
					  size, "realloc", "signatures array");
				return -1;
			}
		}
		signatures[s_count++] = signature;
	}
	/** Sort the list of files */
	qsort(signatures, s_count, sizeof(*signatures), cmp_i64);
	/**
	 * Update the log dir index with the current state:
	 * remove files which no longer exist, add files which
	 * appeared since the last scan.
	 */
	struct vclock *vclock = vclockset_first(&dir->index);
	unsigned i = 0;
	while (i < s_count || vclock != NULL) {
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
				 * panic_if_error must not affect OOM
				 */
				struct error *e = diag_last_error(&fiber()->diag);
				if (dir->panic_if_error ||
				    type_cast(OutOfMemory, e))
					return -1;
				/** Skip a corrupted file */
				error_log(e);
				return 0;
			}
			i++;
		} else {
			assert(s_old == s_new && i < s_count &&
			       vclock != NULL);
			vclock = vclockset_next(&dir->index, vclock);
			i++;
		}
	}
	return 0;
}

int
xdir_check(struct xdir *dir)
{
	DIR *dh = opendir(dir->dirname);        /* log dir */
	if (dh == NULL) {
		tnt_error(SystemError, "error reading directory '%s'",
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

/* }}} */


/* {{{ struct xlog */

static int
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

/**
 * In case of error, writes a message to the server log
 * and sets errno.
 */
struct xlog *
xdir_create_xlog(struct xdir *dir, const struct vclock *vclock)
{
	char *filename;
	int fd = -1;
	struct xlog *l = NULL;
	char meta_buf[XLOG_META_LEN_MAX];
	int meta_len;
	int64_t signature = vclock_sum(vclock);
	assert(signature >= 0);
	assert(!tt_uuid_is_nil(dir->server_uuid));

	/*
	* Check whether a file with this name already exists.
	* We don't overwrite existing files.
	*/
	filename = xdir_format_filename(dir, signature, NONE);
	if (access(filename, F_OK) == 0) {
		errno = EEXIST;
		goto error;
	}

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
	filename = xdir_format_filename(dir, signature, INPROGRESS);
	say_info("creating `%s'", filename);
	fd = open(filename, dir->open_wflags, 0644);
	if (fd < 0)
		goto error;
	l = (struct xlog *) calloc(1, sizeof(*l));
	if (l == NULL)
		goto error;
	l->fd = fd;
	snprintf(l->filename, PATH_MAX, "%s", filename);
	/* Setup inherited values */
	snprintf(l->meta.filetype, sizeof(l->meta.filetype), "%s", dir->filetype);
	l->meta.server_uuid = *dir->server_uuid;
	vclock_copy(&l->meta.vclock, vclock);
	l->sync_is_async = dir->sync_is_async;
	l->sync_interval = dir->sync_interval;
	l->synced_size = 0;

	l->is_inprogress = true;

	l->is_autocommit = true;
	obuf_create(&l->obuf, &cord()->slabc, XLOG_TX_AUTOCOMMIT_THRESHOLD);
	l->zctx = ZSTD_createCCtx();
	if (!l->zctx)
		goto error;
	obuf_create(&l->zbuf, &cord()->slabc, XLOG_TX_AUTOCOMMIT_THRESHOLD);

	/* Format metadata */
	meta_len = xlog_meta_format(&l->meta, meta_buf, sizeof(meta_buf));
	if (meta_len < 0 || meta_len >= (int)sizeof(meta_buf)) {
		say_error("%s: failed to format xlog meta", filename);
		goto error;
	}

	/* Write metadata */
	if (fio_write(l->fd, meta_buf, meta_len) != meta_len)
		goto error;
	l->offset = meta_len; /* first log starts after meta */

	/* Rename xlog file */
	if (dir->suffix != INPROGRESS && xlog_rename(l))
		goto error;

	return l;
error:
	int save_errno = errno;
	say_syserror("%s: failed to open", filename);
	if (fd >= 0) {
		close(fd);
		unlink(filename); /* try to remove incomplete file */
	}
	if (l) {
		obuf_destroy(&l->obuf);
		obuf_destroy(&l->zbuf);
	}
	if (l && l->zctx)
		ZSTD_freeCCtx(l->zctx);
	free(l);
	errno = save_errno;
	return NULL;
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
	if (padding > 0)
		data = mp_encode_strl(data, padding - 1) + padding - 1;

	ssize_t written = fio_writev(log->fd, log->obuf.iov,
				     log->obuf.pos + 1);
	if (written < (ssize_t)obuf_size(&log->obuf)) {
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
			tnt_error(OutOfMemory, zmax_size, "runtime arena",
				  "decompression buffer");
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
		if (ZSTD_isError(zsize))
			goto error;
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
	if (padding > 0)
		data = mp_encode_strl(data, padding - 1) + padding - 1;

	ssize_t written;
	ERROR_INJECT(ERRINJ_WAL_WRITE_DISK,
		     log->zbuf.iov->iov_len >>= 1;);

	written = fio_writev(log->fd, log->zbuf.iov,
			     log->zbuf.pos + 1);
	if (written < (ssize_t)obuf_size(&log->zbuf))
		goto error;
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
	ERROR_INJECT(ERRINJ_WAL_WRITE, written = -1;);

	obuf_reset(&log->obuf);
	log->offset += written > 0 ? written : 0;
	/*
	 * Simplify recovery after a temporary write failure:
	 * truncate the file to the best known good write
	 * position.
	 */
	if (written < 0) {
		if (lseek(log->fd, log->offset, SEEK_SET) < 0 ||
		    ftruncate(log->fd, log->offset) != 0)
			panic_syserror("failed to truncate xlog after write error");
	}
	if (log->sync_interval && log->offset >=
	    (off_t)(log->synced_size + log->sync_interval)) {
		off_t sync_from = SYNC_ROUND_DOWN(log->synced_size);
		size_t sync_len = SYNC_ROUND_UP(log->offset) -
				  sync_from;
		/** sync data from cache to disk */
#ifdef HAVE_SYNC_FILE_RANGE
		sync_file_range(log->fd, sync_from, sync_len,
				SYNC_FILE_RANGE_WAIT_BEFORE |
				SYNC_FILE_RANGE_WRITE |
				SYNC_FILE_RANGE_WAIT_AFTER);
#else
		fdatasync(log->fd);
#endif /* HAVE_SYNC_FILE_RANGE */
#ifdef HAVE_POSIX_FADVISE
		/** free page cache */
		posix_fadvise(log->fd, sync_from, sync_len,
			      POSIX_FADV_DONTNEED);
#else
		(void) sync_from;
		(void) sync_len;
#endif /* HAVE_POSIX_FADVISE */
		log->synced_size = log->offset;
	}
	return written;
}

/*
 * Add a row to a log and possibly flush the log.
 *
 * @retval -1 error
 * @retval >=0 number of bytes flushed to disk by this write.
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
			tnt_error(OutOfMemory, XLOG_FIXHEADER_SIZE,
				  "runtime arena", "xlog tx output buffer");
			return -1;
		}
	}

	/** encode row into iovec */
	struct iovec iov[XROW_IOVMAX];
	int iovcnt = xrow_header_encode(packet, iov, 0);
	struct obuf_svp svp = obuf_create_svp(&log->obuf);
	for (int i = 0; i < iovcnt; ++i) {
		ERROR_INJECT(ERRINJ_WAL_WRITE_PARTIAL,
			{if (obuf_size(&log->obuf) > (1 << 14)) {
				obuf_rollback_to_svp(&log->obuf, &svp);
				return -1;}});
		if (obuf_dup(&log->obuf, iov[i].iov_base, iov[i].iov_len) <
		    iov[i].iov_len) {
			tnt_error(OutOfMemory, XLOG_FIXHEADER_SIZE,
				  "runtime arena", "xlog tx output buffer");
			obuf_rollback_to_svp(&log->obuf, &svp);
			return -1;
		}
	}
	assert(iovcnt <= XROW_IOVMAX);

	if (log->is_autocommit &&
	    obuf_size(&log->obuf) >= XLOG_TX_AUTOCOMMIT_THRESHOLD) {
		return xlog_tx_write(log);
	}
	return 0;
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

int
xlog_close(struct xlog *l)
{
	int rc = fio_write(l->fd, &eof_marker, sizeof(log_magic_t));
	if (rc < 0)
		say_error("Can't finalize xlog %s", l->filename);

	/*
	 * Sync the file before closing, since
	 * otherwise we can end up with a partially
	 * written file in case of a crash.
	 * We sync even if file open O_SYNC, simplify code for low cost
	 */
	xlog_sync(l);

	rc = close(l->fd);
	if (rc < 0)
		say_syserror("%s: close() failed", l->filename);
	obuf_destroy(&l->obuf);
	obuf_destroy(&l->zbuf);
	if (l->zctx)
		ZSTD_freeCCtx(l->zctx);
	free(l);
	return rc;
}

/**
 * Free xlog memory and destroy it cleanly, without side
 * effects (for use in the atfork handler).
 */
void
xlog_atfork(struct xlog **lptr)
{
	struct xlog *l = *lptr;
	if (l) {
		/*
		 * Close the file descriptor STDIO buffer does not
		 * make its way into the respective file in
		 * fclose().
		 */
		close(l->fd);
		*lptr = NULL;
	}
}

/* }}} */

/* {{{ struct xlog_cursor */

#define XLOG_READ_AHEAD		(1 << 14)

/*
 * Loads next count bytes into read buffer,
 * set data to next chunk pointer
 */
static ssize_t
xlog_cursor_load(struct xlog_cursor *cursor, size_t count, char **data)
{
	assert(cursor->buf_offset <= cursor->read_offset);
	if (cursor->buf_offset + ibuf_used(&cursor->rbuf) <
	    (size_t)cursor->read_offset) {
		/* we read something behind of the end of read buffer */
		ibuf_reset(&cursor->rbuf);
		cursor->buf_offset = cursor->read_offset;
	}
	size_t buf_delta = cursor->read_offset - cursor->buf_offset;
	if (ibuf_used(&cursor->rbuf) >= buf_delta + count) {
		*data = cursor->rbuf.rpos + buf_delta;
		cursor->read_offset += count;
		return count;
	}
	/* in-memory mode */
	if (cursor->fd < 0)
		return 0;
	size_t to_load = count - (ibuf_used(&cursor->rbuf) - buf_delta);
	to_load += XLOG_READ_AHEAD;
	void *dst = ibuf_reserve(&cursor->rbuf, to_load);
	if (dst == NULL) {
		diag_set(OutOfMemory, to_load, "runtime",
			 "xlog cursor read buffer");
		return -1;
	}
	ssize_t readen = fio_read(cursor->fd, dst, to_load);
	if (readen < 0) {
		diag_set(SystemError, "failed to read '%s' file",
			 cursor->name);
		return -1;
	}
	/* ibuf_reserve() has been called above, ibuf_alloc() must not fail */
	assert((size_t)readen <= to_load);
	ibuf_alloc(&cursor->rbuf, readen);
	*data = cursor->rbuf.rpos + buf_delta;
	readen = MIN(ibuf_used(&cursor->rbuf) - buf_delta, count);
	cursor->read_offset += readen;
	return readen;
}

/*
 * Remove count bytes from read buffer
 */
static void
xlog_cursor_consume(struct xlog_cursor *cursor, size_t count)
{
	assert(cursor->rbuf.rpos + count <= cursor->rbuf.wpos);
	cursor->buf_offset += count;
	cursor->rbuf.rpos += count;
}

/**
 * Decompress zstd-compressed buf into cursor row block
 */
static int
xlog_cursor_decompress(struct xlog_cursor *i)
{
	if (!ibuf_capacity(&i->zbuf)) {
		if (!ibuf_reserve(&i->zbuf,
				  2 * XLOG_TX_AUTOCOMMIT_THRESHOLD)) {
			tnt_error(OutOfMemory, XLOG_TX_AUTOCOMMIT_THRESHOLD,
				  "runtime", "xlog decompression buffer");
			return -1;
		}
	} else {
		ibuf_reset(&i->zbuf);
	}

	ZSTD_initDStream(i->zdctx);
	ZSTD_inBuffer input = {i->data_pos, (size_t)(i->data_end - i->data_pos), 0};
	while (input.pos < input.size) {
		ZSTD_outBuffer output = {i->zbuf.wpos,
					 ibuf_unused(&i->zbuf),
					 0};
		size_t rc = ZSTD_decompressStream(i->zdctx, &output, &input);
		/* ibuf_reserve() has been called, alloc() must not fail */
		assert(output.pos <= ibuf_unused(&i->zbuf));
		ibuf_alloc(&i->zbuf, output.pos);
		if (ZSTD_isError(rc)) {
			tnt_error(XlogError,
				  "can't decompress xlog tx data with "
				  "code: %i", rc);
			return -1;
		}
		if (input.pos == input.size) {
			break;
		}
		if (output.pos == output.size) {
			if (!ibuf_reserve(&i->zbuf,
					  ibuf_capacity(&i->zbuf))){
				tnt_error(OutOfMemory,
					  2 * ibuf_capacity(&i->zbuf),
					  "runtime",
					  "xlog cursor decompression buffer");
				return -1;
			}
			continue;
		}
	}
	i->data_pos = i->zbuf.rpos;
	i->data_end = i->zbuf.wpos;
	return 0;
}

/**
 * @retval -1 error
 * @retval 0 success
 * @retval 1 EOF
 */
static int
xlog_cursor_read_tx(struct xlog_cursor *i, log_magic_t magic)
{
	ssize_t header_size = XLOG_FIXHEADER_SIZE - sizeof(log_magic_t);
	char *fixheader;
	ssize_t loaded = xlog_cursor_load(i, header_size, &fixheader);
	if (loaded < 0)
		return -1;
	if (loaded < header_size)
		return 1;

	const char *data = fixheader;

	/* Decode len, previous crc32 and row crc32 */
	if (mp_check(&data, data + header_size) != 0) {
	error:
		tnt_error(XlogError, "%s: failed to parse tx "
			  "header at offset %" PRIu64, i->name,
			  (uint64_t)i->search_offset);
		return -1;
	}
	data = fixheader;

	/* Read length */
	if (mp_typeof(*data) != MP_UINT)
		goto error;
	uint32_t len = mp_decode_uint(&data);
	if (len > IPROTO_BODY_LEN_MAX) {
		tnt_error(XlogError,
			 "%s: row is too big at offset %" PRIu64,
			 i->name, (uint64_t)i->search_offset);
		return -1;
	}

	/* Read previous crc32 */
	if (mp_typeof(*data) != MP_UINT)
		goto error;

	/* Read current crc32 */
	uint32_t crc32p = mp_decode_uint(&data);
	if (mp_typeof(*data) != MP_UINT)
		goto error;
	uint32_t crc32c = mp_decode_uint(&data);
	assert(data <= fixheader + header_size);
	(void) crc32p;

	loaded = xlog_cursor_load(i, len, &i->data_pos);
	if (loaded < 0)
		return -1;
	if (loaded < len)
		return 1;
	i->data_end = i->data_pos + len;

	/* Validate checksum */
	if (crc32_calc(0, i->data_pos, len) != crc32c) {
		if (i->ignore_crc == false) {
			tnt_error(XlogError,
				  "%s: row block checksum"
				  " mismatch (expected %u) at offset %"
				  PRIu64,
				  i->name, (unsigned) crc32c,
				  (uint64_t)i->search_offset);
			return -1;
		}
	}

	if (magic == zrow_marker)
		return xlog_cursor_decompress(i);

	return 0;
}

/**
 * Read the next statement (row) from the current transaction.
 */
static int
xlog_cursor_next_row(struct xlog_cursor *i, struct xrow_header *header)
{
	/* Return row from xlog tx buffer */
	int rc = xrow_header_decode(header, (const char **)&i->data_pos,
				    (const char *)i->data_end);
	if (rc != 0) {
		say_warn("failed to read row");
		tnt_error(XlogError, "%s: can't parse row",
			  i->name);
		/* Discard remaining row data */
		i->data_pos = i->data_end;
		return -1;
	}

	return 0;
}

/**
 * Find a next xlog tx magic
 */
int
xlog_cursor_find_tx_magic(struct xlog_cursor *i)
{
	ssize_t loaded, skipped = 0;
	log_magic_t *magic_ptr;
	loaded = xlog_cursor_load(i, sizeof(log_magic_t), (char **)&magic_ptr);
	if (loaded < 0)
		return -1;
	if (loaded < (ssize_t)sizeof(log_magic_t))
		return 1;

	while (*magic_ptr != row_marker && *magic_ptr != zrow_marker) {
		char *last_byte;
		loaded = xlog_cursor_load(i, 1, &last_byte);
		if (loaded < 0)
			return -1;
		if (loaded < 1)
			return 1;
		magic_ptr = (log_magic_t *)(last_byte - sizeof(log_magic_t) + 1);
		/* delete first byte from read buf */
		xlog_cursor_consume(i, 1);
		++skipped;
	}

	if (skipped) {
		say_warn("Skipped %zd after %zd offset untic magic found",
			 skipped, i->search_offset);
	}
	say_debug("magic found at 0x%08jx",
		  (uintmax_t)i->read_offset - sizeof(*magic_ptr));
	return 0;
}

/**
 * Read logfile contents using designated format, panic if
 * the log is corrupted/unreadable.
 *
 * @param i	iterator object, encapsulating log specifics.
 *
 * @retval 0    OK
 * @retval 1    EOF
 */
int
xlog_cursor_next(struct xlog_cursor *i, struct xrow_header *row)
{
	log_magic_t magic = 0;
	int rc;

	assert(i->eof_read == false);

	if (i->data_pos < i->data_end) {
		/*
		 * Still more xrows in this batch, unpack the next
		 * row.
		 */
		rc = xlog_cursor_next_row(i, row);
		if (rc == 0)
			return 0;
		assert(rc < 0);
		return -1;
	}
	/* set read buffer to current pos */
	i->read_offset = i->search_offset;
	xlog_cursor_consume(i, i->read_offset - i->buf_offset);

	log_magic_t *magic_ptr;
	rc = xlog_cursor_load(i, sizeof(log_magic_t), (char **)&magic_ptr);
	if (rc < 0) {
		/**
		 * error by readin byte at read_offset,
		 * restart search from next position
		 */
		i->search_offset = i->read_offset + 1;
		return -1;
	} else if (rc < (int)sizeof(log_magic_t)) {
		goto eof;
	}
	magic = *magic_ptr;
	if (magic == eof_marker)
		goto eof;
	if (magic != zrow_marker && magic != row_marker) {
		/**
		 * There is no marker, possible file corruption,
		 * stop parsing to avoid skiping significant datas
		 */
		tnt_error(XlogError, "%s: invalid marker at %lld",
			  i->name, i->read_offset);
		return -1;
	}
	off_t marker_offset;
	marker_offset = i->read_offset - sizeof(magic);
	rc = xlog_cursor_read_tx(i, magic);
	if (rc > 0) {
		/** eof, go back to marker and retry later */
		i->search_offset = marker_offset;
		goto eof;
	} else if (rc < 0) {
		/** error, can't read byte at read_offset,
		 * go to next and restart later
		 * NOTE: we can have not a some kind of reading issue, but
		 * in any case we can to find next tx and read it
		 */
		say_warn("xlog: failed to read xlog tx at %lld",
			 marker_offset);
		/* start reading from next byte from current marker */
		i->search_offset = marker_offset + 1;
		return -1;
	}
	/* update position */
	i->search_offset = i->read_offset;

	return xlog_cursor_next_row(i, row);
eof:
	i->eof_read = (magic == eof_marker);
	char *some_char;
	if (i->eof_read &&
	    xlog_cursor_load(i, sizeof(log_magic_t) + sizeof(char),
			     &some_char) > 0) {
		tnt_error(XlogError, "%s: has some data after eof "
			  "marker at %lld", i->name, i->read_offset);
		return -1;
	}
	return 1;
}

int
xlog_cursor_openfd(struct xlog_cursor *i, int fd, const char *name)
{
	memset(i, 0, sizeof(*i));
	i->fd = fd;
	i->eof_read = false;
	i->ignore_crc = false;
	ibuf_create(&i->rbuf, &cord()->slabc,
		    XLOG_TX_AUTOCOMMIT_THRESHOLD << 1);

	ibuf_create(&i->zbuf, &cord()->slabc,
		    XLOG_TX_AUTOCOMMIT_THRESHOLD);
	i->zdctx = ZSTD_createDStream();
	ssize_t rc;
	char *meta_start;
	rc = xlog_cursor_load(i, XLOG_META_LEN_MAX, &meta_start);
	if (rc == -1)
		goto error;
	if (rc == 0) {
		tnt_error(XlogError, "Unexpected end of file");
		goto error;
	}
	rc = xlog_meta_parse(&i->meta,
			     (const char **)&i->rbuf.rpos,
			     (const char *)i->rbuf.wpos);
	if (rc == -1)
		goto error;
	if (rc > 0) {
		tnt_error(XlogError, "Unexpected end of file");
		goto error;
	}
	i->read_offset = i->rbuf.rpos - meta_start;
	i->buf_offset = i->read_offset;
	i->search_offset = i->read_offset;
	snprintf(i->name, PATH_MAX, "%s", name);
	return 0;
error:
	ibuf_destroy(&i->rbuf);
	ibuf_destroy(&i->zbuf);
	ZSTD_freeDStream(i->zdctx);
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
	i->eof_read = false;
	i->ignore_crc = false;
	ibuf_create(&i->rbuf, &cord()->slabc,
		    XLOG_TX_AUTOCOMMIT_THRESHOLD << 1);

	void *dst = ibuf_alloc(&i->rbuf, size);
	if (dst == NULL) {
		tnt_error(OutOfMemory, size, "runtime",
			  "xlog cursor read buffer");
		goto error;
	}
	memcpy(dst, data, size);
	ibuf_create(&i->zbuf, &cord()->slabc, XLOG_TX_AUTOCOMMIT_THRESHOLD);
	i->zdctx = ZSTD_createDStream();
	int rc;
	char *meta_start;
	meta_start = i->rbuf.rpos;
	rc = xlog_meta_parse(&i->meta,
			     (const char **)&i->rbuf.rpos,
			     (const char *)i->rbuf.wpos);
	if (rc < 0)
		goto error;
	if (rc > 0) {
		tnt_error(XlogError, "Unexpected end of file");
		goto error;
	}
	i->read_offset = i->rbuf.rpos - meta_start;
	i->buf_offset = i->read_offset;
	i->search_offset = i->read_offset;
	snprintf(i->name, PATH_MAX, "%s", name);
	return 0;
error:
	ibuf_destroy(&i->rbuf);
	ibuf_destroy(&i->zbuf);
	ZSTD_freeDStream(i->zdctx);
	return -1;
}

void
xlog_cursor_close(struct xlog_cursor *i)
{
	if (i->fd >= 0)
		close(i->fd);
	ibuf_destroy(&i->rbuf);
	ibuf_destroy(&i->zbuf);
	ZSTD_freeDStream(i->zdctx);
	region_free(&fiber()->gc);
	TRASH(i);
}

/* }}} */
