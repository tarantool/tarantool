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
#include <tarantool_eio.h>
#include <msgpuck.h>

#include "coio_task.h"
#include "tt_static.h"
#include "error.h"
#include "xrow.h"
#include "iproto_constants.h"
#include "errinj.h"
#include "salad/grp_alloc.h"
#include "trivia/util.h"
#include "retention_period.h"

/*
 * FALLOC_FL_KEEP_SIZE flag has existed since fallocate() was
 * first introduced, but it was not defined in glibc headers
 * for a while. Define it manually if necessary.
 */
#ifdef HAVE_FALLOCATE
# ifndef FALLOC_FL_KEEP_SIZE
#  define FALLOC_FL_KEEP_SIZE 0x01
# endif
#endif /* HAVE_FALLOCATE */

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

const struct xlog_opts xlog_opts_default = {
	.rate_limit = 0,
	.sync_interval = 0,
	.free_cache = false,
	.sync_is_async = false,
	.no_compression = false,
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
#define PREV_VCLOCK_KEY "PrevVClock"

static const char v13[] = "0.13";
static const char v12[] = "0.12";

void
xlog_meta_create(struct xlog_meta *meta, const char *filetype,
		 const struct tt_uuid *instance_uuid,
		 const struct vclock *vclock,
		 const struct vclock *prev_vclock)
{
	snprintf(meta->filetype, sizeof(meta->filetype), "%s", filetype);
	meta->instance_uuid = *instance_uuid;
	if (vclock != NULL)
		vclock_copy(&meta->vclock, vclock);
	else
		vclock_clear(&meta->vclock);
	if (prev_vclock != NULL)
		vclock_copy(&meta->prev_vclock, prev_vclock);
	else
		vclock_clear(&meta->prev_vclock);
}

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
	int total = 0;
	SNPRINT(total, snprintf, buf, size,
		"%s\n"
		"%s\n"
		VERSION_KEY ": %s\n"
		INSTANCE_UUID_KEY ": %s\n",
		meta->filetype, v13, PACKAGE_VERSION,
		tt_uuid_str(&meta->instance_uuid));
	if (vclock_is_set(&meta->vclock)) {
		SNPRINT(total, snprintf, buf, size, VCLOCK_KEY ": %s\n",
			vclock_to_string(&meta->vclock));
	}
	if (vclock_is_set(&meta->prev_vclock)) {
		SNPRINT(total, snprintf, buf, size, PREV_VCLOCK_KEY ": %s\n",
			vclock_to_string(&meta->prev_vclock));
	}
	SNPRINT(total, snprintf, buf, size, "\n");
	assert(total > 0);
	return total;
}

/**
 * Parse vclock from xlog meta.
 */
static int
parse_vclock(const char *val, const char *val_end, struct vclock *vclock)
{
	if (val_end - val > VCLOCK_STR_LEN_MAX) {
		diag_set(XlogError, "can't parse vclock");
		return -1;
	}
	char str[VCLOCK_STR_LEN_MAX + 1];
	memcpy(str, val, val_end - val);
	str[val_end - val] = '\0';
	size_t off = vclock_from_string(vclock, str);
	ERROR_INJECT(ERRINJ_XLOG_META, { off = 1; });
	if (off != 0) {
		diag_set(XlogError, "invalid vclock at "
			 "offset %zd", off);
		return -1;
	}
	return 0;
}

static inline bool
xlog_meta_key_equal(const char *key, const char *key_end, const char *str)
{
	size_t key_len = key_end - key;
	return key_len == strlen(str) && memcmp(key, str, key_len) == 0;
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

	vclock_clear(&meta->vclock);
	vclock_clear(&meta->prev_vclock);

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

		if (xlog_meta_key_equal(key, key_end, INSTANCE_UUID_KEY) ||
		    xlog_meta_key_equal(key, key_end, INSTANCE_UUID_KEY_V12)) {
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
		} else if (xlog_meta_key_equal(key, key_end, VCLOCK_KEY)) {
			/*
			 * VClock: <vclock>
			 */
			if (parse_vclock(val, val_end, &meta->vclock) != 0)
				return -1;
		} else if (xlog_meta_key_equal(key, key_end, PREV_VCLOCK_KEY)) {
			/*
			 * PrevVClock: <vclock>
			 */
			if (parse_vclock(val, val_end, &meta->prev_vclock) != 0)
				return -1;
		} else if (xlog_meta_key_equal(key, key_end, VERSION_KEY)) {
			/* Ignore Version: for now */
		} else {
			/*
			 * Unknown key
			 */
			say_warn("Unknown meta item: '%.*s'",
				 (int)(key_end - key), key);
		}
	}
	*data = end + 1; /* skip the last trailing \n of \n\n sequence */
	return 0;
}

/* struct xlog }}} */

/* {{{ struct xdir */

void
xdir_create(struct xdir *dir, const char *dirname, const char *filetype,
	    const struct tt_uuid *instance_uuid, const struct xlog_opts *opts)
{
	memset(dir, 0, sizeof(*dir));
	dir->opts = *opts;
	vclockset_new(&dir->index);
	/* Default mode. */
	dir->mode = 0660;
	dir->instance_uuid = instance_uuid;
	snprintf(dir->dirname, sizeof(dir->dirname), "%s", dirname);
	dir->open_wflags = 0;
	VERIFY((size_t)snprintf(dir->filetype, sizeof(dir->filetype),
				"%s", filetype) < sizeof(dir->filetype));
	VERIFY((size_t)snprintf(dir->filename_ext, sizeof(dir->filename_ext),
				".%s", filetype) < sizeof(dir->filename_ext));
	strtolower(dir->filename_ext);
	dir->retention_period = 0;
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
	 * Append the clock describing the file to the directory index.
	 * If retention feature is available (EE), then additional memory is
	 * allocated. Time, when xlog protection should stop is saved there.
	 */
	struct vclock *vclock = retention_vclock_new();
	vclock_copy(vclock, &meta->vclock);
	xlog_cursor_close(&cursor, false);
	vclockset_insert(&dir->index, vclock);
	return 0;
}

int
xdir_open_cursor(struct xdir *dir, int64_t signature,
		 struct xlog_cursor *cursor)
{
	const char *filename = xdir_format_filename(dir, signature);
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
 * @param dir - directory to scan
 * @param is_dir_required - flag set if the directory should exist
 *
 * @return:
 *   0 - on success or flag 'is_dir_required' was set to False in
 *   xdir_scan() arguments and opendir() failed with errno ENOENT;
 *   -1 - if opendir() failed, with other than ENOENT errno either
 *   when 'is_dir_required' was set to True in xdir_scan() arguments.
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
 */
int
xdir_scan(struct xdir *dir, bool is_dir_required)
{
	DIR *dh = opendir(dir->dirname);        /* log dir */
	int64_t *signatures = NULL;             /* log file names */
	size_t s_count = 0, s_capacity = 0;

	if (dh == NULL) {
		if (!is_dir_required && errno == ENOENT)
			return 0;
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
	if (s_count > 1)
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
	/* Initialize expiration time of all files, saved inside index. */
	retention_index_update(dir, 0);
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

void
xdir_set_retention_period(struct xdir *xdir, double period)
{
	double old_period = xdir->retention_period;
	xdir->retention_period = period;
	retention_index_update(xdir, old_period);
}

void
xdir_get_retention_vclock(struct xdir *xdir, struct vclock *vclock)
{
	retention_index_get(&xdir->index, vclock);
}

void
xdir_set_retention_vclock(struct xdir *xdir, struct vclock *vclock)
{
	if (xdir->retention_period == 0)
		return;

	struct vclock *find = vclockset_match(&xdir->index, vclock);
	assert(vclock_compare(find, vclock) == 0);
	retention_vclock_set(find, xdir->retention_period);
}

const char *
xdir_format_filename(struct xdir *dir, int64_t signature)
{
	return tt_snprintf(PATH_MAX, "%s/%020lld%s",
			   dir->dirname, (long long)signature,
			   dir->filename_ext);
}

void
xdir_collect_garbage(struct xdir *dir, int64_t signature, unsigned flags)
{
	unsigned rm_flags = XLOG_RM_VERBOSE;
	if (flags & XDIR_GC_ASYNC)
		rm_flags |= XLOG_RM_ASYNC;

	struct vclock retention_vclock;
	retention_index_get(&dir->index, &retention_vclock);
	if (vclock_is_set(&retention_vclock) &&
	    signature > vclock_sum(&retention_vclock)) {
		/*
		 * This should not normally happen and can be a reason of
		 * xdir_retention misusage: retention_vclock must be always
		 * checked before invoking garbage collection. As we cannot
		 * return error code from this function (gc already updated
		 * its vclock), the best we can do is warning.
		 */
		assert(false);
		say_warn("Requested xdir gc vclock is greater than "
			 "retention vclock. Using retention vclock instead");
		signature = vclock_sum(&retention_vclock);
	}

	struct vclock *vclock;
	while ((vclock = vclockset_first(&dir->index)) != NULL &&
	       vclock_sum(vclock) < signature) {
		const char *filename =
			xdir_format_filename(dir, vclock_sum(vclock));
		xlog_remove_file(filename, rm_flags);
		vclockset_remove(&dir->index, vclock);
		free(vclock);
		if (flags & XDIR_GC_REMOVE_ONE)
			break;
	}
}

int
xdir_remove_file_by_vclock(struct xdir *dir, struct vclock *to_remove)
{
	struct vclock *find = vclockset_match(&dir->index, to_remove);
	if (vclock_compare(find, to_remove) != 0)
		return -1;
	const char *filename = xdir_format_filename(dir, vclock_sum(find));
	if (!xlog_remove_file(filename, XLOG_RM_VERBOSE))
		return -1;
	vclockset_remove(&dir->index, find);
	free(find);
	return 0;
}

/** Default implementation of xlog_file_is_temporary(). */
static bool
xlog_file_is_temporary_default(const char *filename)
{
	char *ext = strrchr(filename, '.');
	return ext != NULL && strcmp(ext, inprogress_suffix) == 0;
}

xlog_file_is_temporary_f xlog_file_is_temporary =
				xlog_file_is_temporary_default;

void
xdir_remove_temporary_files(struct xdir *xdir)
{
	const char *dirname = xdir->dirname;
	DIR *dh = opendir(dirname);
	if (dh == NULL) {
		if (errno != ENOENT)
			say_syserror("error reading directory '%s'", dirname);
		return;
	}
	struct dirent *dent;
	while ((dent = readdir(dh)) != NULL) {
		const char *filename = tt_snprintf(PATH_MAX, "%s/%s",
						   dirname, dent->d_name);
		if (xlog_file_is_temporary(filename))
			xlog_remove_file(filename, XLOG_RM_VERBOSE);
	}
	closedir(dh);
}

void
xdir_add_vclock(struct xdir *xdir, const struct vclock *vclock)
{
	struct vclock *copy = retention_vclock_new();
	vclock_copy(copy, vclock);
	vclockset_insert(&xdir->index, copy);
}

/* }}} */


/* {{{ struct xlog */

int
xlog_materialize(struct xlog *l)
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

	ERROR_INJECT_SLEEP(ERRINJ_XLOG_RENAME_DELAY);

	if (rename(filename, new_filename) != 0) {
		say_syserror("can't rename %s to %s", filename, new_filename);
		diag_set(SystemError, "failed to rename '%s' file",
				filename);
		return -1;
	}
	l->is_inprogress = false;
	filename[suffix - filename] = '\0';
	return 0;
}

static int
xlog_init(struct xlog *xlog, const struct xlog_opts *opts)
{
	memset(xlog, 0, sizeof(*xlog));
	xlog->opts = *opts;
	xlog->sync_time = ev_monotonic_time();
	xlog->is_autocommit = true;
	obuf_create(&xlog->obuf, &cord()->slabc, XLOG_TX_AUTOCOMMIT_THRESHOLD);
	obuf_create(&xlog->zbuf, &cord()->slabc, XLOG_TX_AUTOCOMMIT_THRESHOLD);
	if (!opts->no_compression) {
		xlog->zctx = ZSTD_createCCtx();
		if (xlog->zctx == NULL) {
			diag_set(ClientError, ER_COMPRESSION,
				 "failed to create context");
			return -1;
		}
	}
	return 0;
}

void
xlog_clear(struct xlog *l)
{
	memset(l, 0, sizeof(*l));
	l->fd = -1;
}

/**
 * Frees the write context allocated in xlog_init().
 * The xlog file must be closed.
 */
static void
xlog_free(struct xlog *xlog)
{
	assert(xlog->fd < 0);
	assert(xlog->obuf.slabc == &cord()->slabc);
	assert(xlog->zbuf.slabc == &cord()->slabc);
	obuf_destroy(&xlog->obuf);
	obuf_destroy(&xlog->zbuf);
	ZSTD_freeCCtx(xlog->zctx);
	xlog->zctx = NULL;
}

int
xlog_create(struct xlog *xlog, const char *name, int flags,
	    const struct xlog_meta *meta, const struct xlog_opts *opts)
{
	char meta_buf[XLOG_META_LEN_MAX];
	int meta_len;

	/*
	 * Check whether a file with this name already exists.
	 * We don't overwrite existing files.
	 */
	if (access(name, F_OK) == 0) {
		errno = EEXIST;
		diag_set(SystemError, "file '%s' already exists", name);
		goto err;
	}

	if (xlog_init(xlog, opts) != 0)
		goto err;

	xlog->meta = *meta;
	xlog->is_inprogress = true;
	snprintf(xlog->filename, sizeof(xlog->filename), "%s%s", name, inprogress_suffix);

	/* Make directory if needed (gh-5090). */
	if (mkdirpath(xlog->filename) != 0) {
		diag_set(SystemError, "failed to create path '%s'",
			 xlog->filename);
		goto err;
	}

	flags |= O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC;

	/*
	 * Open the <lsn>.<suffix>.inprogress file.
	 * If it exists, open will fail. Always open/create
	 * a file with .inprogress suffix: for snapshots,
	 * the rename is done when the snapshot is complete.
	 * For xlogs, we can rename only when we have written
	 * the log file header, otherwise replication relay
	 * may think that this is a corrupt file and stop
	 * replication.
	 */
	xlog->fd = open(xlog->filename, flags, 0644);
	if (xlog->fd < 0) {
		diag_set(SystemError, "failed to create file '%s'",
			 xlog->filename);
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
		diag_set(SystemError, "%s: failed to write xlog meta",
			 xlog->filename);
		goto err_write;
	}

	xlog->offset = meta_len; /* first log starts after meta */
	return 0;
err_write:
	close(xlog->fd);
	xlog->fd = -1;
	xlog_remove_file(xlog->filename, 0); /* try to remove incomplete file */
err_open:
	xlog_free(xlog);
err:
	return -1;
}

int
xlog_open(struct xlog *xlog, const char *name, const struct xlog_opts *opts)
{
	char magic[sizeof(log_magic_t)];
	char meta_buf[XLOG_META_LEN_MAX];
	const char *meta = meta_buf;
	int meta_len;
	int rc;

	if (xlog_init(xlog, opts) != 0)
		goto err;

	strlcpy(xlog->filename, name, sizeof(xlog->filename));

	xlog->fd = open(xlog->filename, O_RDWR | O_CLOEXEC);
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

	/* Check if the file has EOF marker. */
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
	} else {
		/* Truncate the file to erase the EOF marker. */
		if (ftruncate(xlog->fd, xlog->offset) != 0) {
			diag_set(SystemError, "failed to truncate file '%s'",
				 xlog->filename);
			goto err_read;
		}
	}
	return 0;
err_read:
	close(xlog->fd);
	xlog->fd = -1;
err_open:
	xlog_free(xlog);
err:
	return -1;
}

int
xdir_touch_xlog(struct xdir *dir, const struct vclock *vclock)
{
	int64_t signature = vclock_sum(vclock);
	const char *filename = xdir_format_filename(dir, signature);
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
	int64_t signature = vclock_sum(vclock);
	assert(signature >= 0);
	assert(!tt_uuid_is_nil(dir->instance_uuid));

	const struct vclock *prev_vclock = NULL;
	if (dir->store_prev_vclock && !vclockset_empty(&dir->index))
		prev_vclock = vclockset_last(&dir->index);

	struct xlog_meta meta;
	xlog_meta_create(&meta, dir->filetype, dir->instance_uuid,
			 vclock, prev_vclock);

	const char *filename = xdir_format_filename(dir, signature);
	return xlog_create(xlog, filename, dir->open_wflags, &meta, &dir->opts);
}

int
xdir_create_materialized_xlog(struct xdir *dir, struct xlog *xlog,
			      const struct vclock *vclock)
{
	if (xdir_create_xlog(dir, xlog, vclock) != 0)
		return -1;
	if (xlog_materialize(xlog) != 0) {
		xlog_discard(xlog);
		return -1;
	}
	return 0;
}

ssize_t
xlog_fallocate(struct xlog *log, size_t len)
{
#ifdef HAVE_FALLOCATE
	static bool fallocate_not_supported = false;
	if (fallocate_not_supported)
		return 0;
	/*
	 * Keep the file size, because it is used to sync
	 * concurrent readers vs the writer: xlog_cursor
	 * assumes that everything written before EOF is
	 * valid data.
	 */
	int rc = fallocate(log->fd, FALLOC_FL_KEEP_SIZE,
			   log->offset + log->allocated, len);
	if (rc != 0) {
		if (errno == ENOSYS || errno == EOPNOTSUPP) {
			say_warn("fallocate is not supported, "
				 "proceeding without it");
			fallocate_not_supported = true;
			return 0;
		}
		diag_set(SystemError, "%s: can't allocate disk space",
			 log->filename);
		return -1;
	}
	log->allocated += len;
	return 0;
#else
	(void)log;
	(void)len;
	return 0;
#endif /* HAVE_FALLOCATE */
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
	memcpy(fixheader, &row_marker, sizeof(log_magic_t));
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
	if (fixheader == NULL) {
		diag_set(OutOfMemory, XLOG_FIXHEADER_SIZE, "runtime arena",
			 "compression buffer");
		goto error;
	}
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

	memcpy(fixheader, &zrow_marker, sizeof(log_magic_t));
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

	if (!log->opts.no_compression &&
	    obuf_size(&log->obuf) >= XLOG_TX_COMPRESS_THRESHOLD) {
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
		log->allocated = 0;
		return -1;
	}
	if (log->allocated > (size_t)written)
		log->allocated -= written;
	else
		log->allocated = 0;
	log->offset += written;
	log->rows += log->tx_rows;
	log->tx_rows = 0;
	if ((log->opts.sync_interval && log->offset >=
	    (off_t)(log->synced_size + log->opts.sync_interval)) ||
	    (log->opts.rate_limit && log->offset >=
	    (off_t)(log->synced_size + log->opts.rate_limit))) {
		off_t sync_from = SYNC_ROUND_DOWN(log->synced_size);
		size_t sync_len = SYNC_ROUND_UP(log->offset) -
				  sync_from;
		if (log->opts.rate_limit > 0) {
			double throttle_time;
			throttle_time = (double)sync_len / log->opts.rate_limit -
					(ev_monotonic_time() - log->sync_time);
			if (throttle_time > 0)
				ev_sleep(throttle_time);
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
		if (log->opts.free_cache) {
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
	size_t region_svp = region_used(&fiber()->gc);
	int iovcnt;
	xrow_encode(packet, /*sync=*/0, /*fixheader_len=*/0, iov, &iovcnt);
	for (int i = 0; i < iovcnt; ++i) {
		struct errinj *inj = errinj(ERRINJ_WAL_WRITE_PARTIAL,
					    ERRINJ_INT);
		if (inj != NULL && inj->iparam >= 0 &&
		    obuf_size(&log->obuf) > (size_t)inj->iparam) {
			diag_set(ClientError, ER_INJECTION,
				 "xlog write injection");
			obuf_rollback_to_svp(&log->obuf, &svp);
			region_truncate(&fiber()->gc, region_svp);
			return -1;
		};
		if (obuf_dup(&log->obuf, iov[i].iov_base, iov[i].iov_len) <
		    iov[i].iov_len) {
			diag_set(OutOfMemory, XLOG_FIXHEADER_SIZE,
				  "runtime arena", "xlog tx output buffer");
			obuf_rollback_to_svp(&log->obuf, &svp);
			region_truncate(&fiber()->gc, region_svp);
			return -1;
		}
	}
	region_truncate(&fiber()->gc, region_svp);
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

/**
 * Syncs an xlog object to disk.
 *
 * If the sync_is_async flag is set in xlog_opts, fsync is called
 * asynchronously, without checking the result.
 *
 * Returns 0 on success. On failure, sets diag returns -1.
 */
static int
xlog_sync(struct xlog *l)
{
	if (l->opts.sync_is_async) {
		int fd = dup(l->fd);
		if (fd == -1) {
			diag_set(SystemError, "failed to duplicate fd %d",
				 l->fd);
			return -1;
		}
		eio_fsync(fd, 0, sync_cb, (void *) (intptr_t) fd);
	} else if (fsync(l->fd) < 0) {
		diag_set(SystemError, "failed to sync file '%s'", l->filename);
		return -1;
	}
	return 0;
}

/**
 * Writes the EOF marker to the xlog file.
 *
 * Returns 0 on success. On failure, sets diag returns -1.
 */
static int
xlog_write_eof(struct xlog *l)
{
	ERROR_INJECT(ERRINJ_WAL_WRITE_EOF, return 0);

	/*
	 * Free disk space preallocated with xlog_fallocate().
	 * Don't write the eof marker if this fails, otherwise
	 * we'll get "data after eof marker" error on recovery.
	 */
	if (l->allocated > 0 && ftruncate(l->fd, l->offset) < 0) {
		diag_set(SystemError, "failed to truncate file '%s'",
			 l->filename);
		return -1;
	}

	if (fio_writen(l->fd, &eof_marker, sizeof(eof_marker)) < 0) {
		diag_set(SystemError, "failed to write to file '%s'",
			 l->filename);
		return -1;
	}
	return 0;
}

int
xlog_close_reuse_fd(struct xlog *l, int *fd)
{
	assert(l->fd >= 0);
	int rc = xlog_flush(l) < 0 ? -1 : 0;
	if (rc == 0)
		rc = xlog_write_eof(l);
	if (rc == 0)
		rc = xlog_sync(l);
	*fd = l->fd;
	l->fd = -1;
	xlog_free(l);
	return rc;
}

int
xlog_close(struct xlog *l)
{
	int fd;
	int rc = xlog_close_reuse_fd(l, &fd);
	close(fd);
	return rc;
}

void
xlog_discard(struct xlog *l)
{
	if (l->fd >= 0) {
		close(l->fd);
		l->fd = -1;
		xlog_free(l);
	}
	if (l->filename[0] != '\0') {
		assert(l->is_inprogress);
		xlog_remove_file(l->filename, 0);
		l->filename[0] = '\0';
	}
}

/* }}} */

/* {{{ struct xlog_cursor */

enum {
	/**
	 * Min and max values for xlog_cursor::read_ahead.
	 */
	XLOG_READ_AHEAD_MIN = XLOG_TX_AUTOCOMMIT_THRESHOLD,
	XLOG_READ_AHEAD_MAX = 8 * 1024 * 1024,
};

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
	to_load += cursor->read_ahead;

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
	assert((size_t)readen <= to_load);
	/* ibuf_reserve() has been called above, ibuf_alloc() must not fail */
	ibuf_alloc(&cursor->rbuf, readen);
	/* Shrink the read buffer to reduce the memory consumption. */
	if (cursor->need_rbuf_shrink) {
		ibuf_shrink(&cursor->rbuf);
		cursor->need_rbuf_shrink = false;
	}
	/*
	 * Grow readahead size if the requested number of bytes was successfully
	 * read, and decrease it to the minimum otherwise.
	 */
	if ((size_t)readen == to_load) {
		if (cursor->read_ahead * 2 <= XLOG_READ_AHEAD_MAX)
			cursor->read_ahead *= 2;
	} else {
		cursor->need_rbuf_shrink = true;
		cursor->read_ahead = XLOG_READ_AHEAD_MIN;
	}
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
	int rc = xrow_decode(xrow, (const char **)&tx_cursor->rows.rpos,
			     (const char *)tx_cursor->rows.wpos, false);
	if (rc != 0) {
		diag_set(XlogError, "can't parse row");
		/* Discard remaining row data */
		ibuf_reset(&tx_cursor->rows);
		return -1;
	}

	return 0;
}

int
xlog_tx_cursor_next_row_raw(struct xlog_tx_cursor *tx_cursor,
			    const char ***data, const char **end)
{
	if (ibuf_used(&tx_cursor->rows) == 0)
		return 1;
	*data = (const char **)&tx_cursor->rows.rpos;
	*end = (const char *)tx_cursor->rows.wpos;
	return 0;
}

int
xlog_tx_cursor_destroy(struct xlog_tx_cursor *tx_cursor)
{
	assert(tx_cursor->rows.slabc == &cord()->slabc);
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
	if (i->state == XLOG_CURSOR_TX) {
		i->state = XLOG_CURSOR_ACTIVE;
		xlog_tx_cursor_destroy(&i->tx_cursor);
	}
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
	return xlog_tx_cursor_next_row(&cursor->tx_cursor, xrow);
}

int
xlog_cursor_next_row_raw(struct xlog_cursor *cursor,
			 const char ***data, const char **end)
{
	assert(xlog_cursor_is_open(cursor));
	if (cursor->state != XLOG_CURSOR_TX)
		return 1;
	return xlog_tx_cursor_next_row_raw(&cursor->tx_cursor, data, end);
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
	i->read_ahead = XLOG_READ_AHEAD_MIN;
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
	snprintf(i->name, sizeof(i->name), "%s", name);
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
	snprintf(i->name, sizeof(i->name), "%s", name);
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
	assert(i->rbuf.slabc == &cord()->slabc);
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

/* {{{ xlog_remove_file */

/** xlog_remove_file() coio task. */
struct xlog_remove_file_task {
	/** Base class. */
	struct coio_task base;
	/** Path to the file. */
	char *filename;
	/** Bitwise combination of XLOG_RM_* flags. */
	unsigned flags;
};

/** Default implementation of xlog_remove_file_impl(). */
static int
xlog_remove_file_impl_default(const char *filename, bool *existed)
{
	if (unlink(filename) != 0) {
		if (errno != ENOENT) {
			diag_set(SystemError, "failed to unlink file '%s'",
				 filename);
			return -1;
		}
		*existed = false;
	} else {
		*existed = true;
	}
	return 0;
}

xlog_remove_file_impl_f xlog_remove_file_impl = xlog_remove_file_impl_default;

/** Blocking implementation of xlog_remove_file(). */
static bool
xlog_remove_file_blocking(const char *filename, unsigned flags)
{
	bool existed;
	if (xlog_remove_file_impl(filename, &existed) != 0) {
		diag_log();
		diag_clear(diag_get());
		say_error("error while removing %s", filename);
		return false;
	}
	if (existed && (flags & XLOG_RM_VERBOSE) != 0)
		say_info("removed %s", filename);
	return true;
}

static int
xlog_remove_file_cb(struct coio_task *base)
{
	struct xlog_remove_file_task *task =
		(struct xlog_remove_file_task *)base;
	xlog_remove_file_blocking(task->filename, task->flags);
	return 0;
}

static int
xlog_remove_file_done_cb(struct coio_task *base)
{
	struct xlog_remove_file_task *task =
		(struct xlog_remove_file_task *)base;
	TRASH(task);
	free(task);
	return 0;
}

/** Asynchronous implementation of xlog_remove_file(). */
static bool
xlog_remove_file_async(const char *filename, unsigned flags)
{
	struct xlog_remove_file_task *task;
	struct grp_alloc all = grp_alloc_initializer();
	grp_alloc_reserve_data(&all, sizeof(*task));
	grp_alloc_reserve_str0(&all, filename);
	grp_alloc_use(&all, xmalloc(grp_alloc_size(&all)));
	task = grp_alloc_create_data(&all, sizeof(*task));
	task->filename = grp_alloc_create_str0(&all, filename);
	task->flags = flags;
	assert(grp_alloc_size(&all) == 0);
	coio_task_create(&task->base, xlog_remove_file_cb,
			 xlog_remove_file_done_cb);
	coio_task_post(&task->base);
	return true;
}

bool
xlog_remove_file(const char *filename, unsigned flags)
{
	if (flags & XLOG_RM_ASYNC)
		return xlog_remove_file_async(filename, flags);
	else
		return xlog_remove_file_blocking(filename, flags);
}

/* }}} */
