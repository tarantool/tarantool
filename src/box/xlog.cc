/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "fiob.h"
#include "third_party/tarantool_eio.h"
#include "msgpuck/msgpuck.h"
#include "scoped_guard.h"
#include "xrow.h"
#include "iproto_constants.h"

/*
 * marker is MsgPack fixext2
 * +--------+--------+--------+--------+
 * |  0xd5  |  type  |       data      |
 * +--------+--------+--------+--------+
 */
typedef uint32_t log_magic_t;

static const log_magic_t row_marker = mp_bswap_u32(0xd5ba0bab); /* host byte order */
static const log_magic_t eof_marker = mp_bswap_u32(0xd510aded); /* host byte order */
static const char inprogress_suffix[] = ".inprogress";
static const char v12[] = "0.12\n";

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

/* {{{ struct xdir */

void
xdir_create(struct xdir *dir, const char *dirname,
	    enum xdir_type type, const tt_uuid *server_uuid)
{
	memset(dir, 0, sizeof(*dir));
	vclockset_new(&dir->index);
	/* Default mode. */
	dir->mode = 0660;
	dir->server_uuid = server_uuid;
	if (abspath_inplace(dirname, dir->dirname, sizeof dir->dirname) == -1)
		panic_syserror("getcwd");
	if (type == SNAP) {
		strcpy(dir->open_wflags, "wxd");
		dir->filetype = "SNAP\n";
		dir->filename_ext = ".snap";
		dir->panic_if_error = true;
		dir->suffix = INPROGRESS;
	} else {
		strcpy(dir->open_wflags, "wx");
		dir->sync_is_async = true;
		dir->filetype = "XLOG\n";
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
static inline void
xdir_index_file(struct xdir *dir, int64_t signature)
{
	/*
	 * Open xlog and parse vclock in its text header.
	 * The vclock stores the state of the log at the
	 * time it is created.
	 */
	struct xlog *wal;

	try {
		wal = xlog_open(dir, signature);
		/*
		 * All log files in a directory must satisfy Lamport's
		 * eventual order: events in each log file must be
		 * separable with consistent cuts, for example:
		 *
		 * log1: {1, 1, 0, 1}, log2: {1, 2, 0, 2} -- good
		 * log2: {1, 1, 0, 1}, log2: {2, 0, 2, 0} -- bad
		 */
		struct vclock *dup = vclockset_search(&dir->index,
						      &wal->vclock);
		if (dup != NULL) {
			XlogError *e = tnt_error(XlogError,
						 "%s: invalid xlog order",
						 wal->filename);
			xlog_close(wal);
			throw e;
		}
	} catch (XlogError *e) {
		if (dir->panic_if_error)
			throw;
		/** Skip a corrupted file */
		e->log();
		return;
	}

	auto log_guard = make_scoped_guard([=]{
		xlog_close(wal);
	});

	/*
	 * Append the clock describing the file to the
	 * directory index.
	 */
	struct vclock *vclock = vclock_dup(&wal->vclock);
	vclockset_insert(&dir->index, vclock);
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
void
xdir_scan(struct xdir *dir)
{
	DIR *dh = opendir(dir->dirname);        /* log dir */
	int64_t *signatures = NULL;             /* log file names */
	size_t s_count = 0, s_capacity = 0;

	if (dh == NULL) {
		tnt_raise(SystemError, "error reading directory '%s'",
			  dir->dirname);
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
				tnt_raise(OutOfMemory,
					  size, "realloc", "signatures array");
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
	int i = 0;
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
			xdir_index_file(dir, s_new);
			i++;
		} else {
			assert(s_old == s_new && i < s_count &&
			       vclock != NULL);
			vclock = vclockset_next(&dir->index, vclock);
			i++;
		}
	}
}

void
xdir_check(struct xdir *dir)
{
	DIR *dh = opendir(dir->dirname);        /* log dir */
	if (dh == NULL) {
		tnt_raise(SystemError, "error reading directory '%s'",
			  dir->dirname);
	}
	closedir(dh);
}

char *
format_filename(struct xdir *dir, int64_t signature,
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

/* {{{ struct xlog_cursor */

/**
 * @retval 0 success
 * @retval 1 EOF
 */
static int
row_reader(FILE *f, struct xrow_header *row)
{
	const char *data;

	/* Read fixed header */
	char fixheader[XLOG_FIXHEADER_SIZE - sizeof(log_magic_t)];
	if (fread(fixheader, sizeof(fixheader), 1, f) != 1) {
		if (feof(f))
			return 1;
error:
		char buf[PATH_MAX];
		snprintf(buf, sizeof(buf), "%s: failed to read or parse row header"
			 " at offset %" PRIu64, fio_filename(fileno(f)),
			 (uint64_t) ftello(f));
		tnt_raise(ClientError, ER_INVALID_MSGPACK, buf);
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
		char buf[PATH_MAX];
		snprintf(buf, sizeof(buf),
			 "%s: row is too big at offset %" PRIu64,
			 fio_filename(fileno(f)), (uint64_t) ftello(f));
		tnt_raise(ClientError, ER_INVALID_MSGPACK, buf);
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
	char *bodybuf = (char *) region_alloc_xc(&fiber()->gc, len);

	/* Read header and body */
	if (fread(bodybuf, len, 1, f) != 1)
		return 1;

	/* Validate checksum */
	if (crc32_calc(0, bodybuf, len) != crc32c) {
		char buf[PATH_MAX];

		snprintf(buf, sizeof(buf), "%s: row checksum mismatch (expected %u)"
			 " at offset %" PRIu64,
			 fio_filename(fileno(f)), (unsigned) crc32c,
			 (uint64_t) ftello(f));
		tnt_raise(ClientError, ER_INVALID_MSGPACK, buf);
	}

	data = bodybuf;
	xrow_header_decode(row, &data, bodybuf + len);

	return 0;
}

int
xlog_encode_row(const struct xrow_header *row, struct iovec *iov)
{
	int iovcnt = xrow_header_encode(row, iov, XLOG_FIXHEADER_SIZE);
	char *fixheader = (char *) iov[0].iov_base;
	uint32_t len = iov[0].iov_len - XLOG_FIXHEADER_SIZE;
	uint32_t crc32p = 0;
	uint32_t crc32c = crc32_calc(0, fixheader + XLOG_FIXHEADER_SIZE, len);
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

	assert(iovcnt <= XROW_IOVMAX);
	return iovcnt;
}

void
xlog_cursor_open(struct xlog_cursor *i, struct xlog *l)
{
	i->log = l;
	i->row_count = 0;
	i->good_offset = ftello(l->f);
	i->eof_read  = false;
}

void
xlog_cursor_close(struct xlog_cursor *i)
{
	struct xlog *l = i->log;
	l->rows += i->row_count;
	l->eof_read = i->eof_read;
	/*
	 * Since we don't close the xlog
	 * we must rewind it to the last known
	 * good position if there was an error.
	 * Seek back to last known good offset.
	 */
	fseeko(l->f, i->good_offset, SEEK_SET);
	region_free(&fiber()->gc);
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
	struct xlog *l = i->log;
	log_magic_t magic;
	off_t marker_offset = 0;

	assert(i->eof_read == false);

	say_debug("xlog_cursor_next: marker:0x%016X/%zu",
		  row_marker, sizeof(row_marker));

	/*
	 * Don't let gc pool grow too much. Yet to
	 * it before reading the next row, to make
	 * sure it's not freed along here.
	 */
	region_free_after(&fiber()->gc, 128 * 1024);

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
	} catch (ClientError *e) {
		if (l->dir->panic_if_error)
			throw;
		/*
		 * say_warn() is used and exception is not
		 * logged since an error may happen in local
		 * hot standby or replication mode, when it's
		 * caused by an attempt to read a partially
		 * written WAL.
		 */
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
	 * According to POSIX, if a partial element is read, value
	 * of the file position indicator for the stream is
	 * unspecified.
	 *
	 * Thus don't trust the current file position, seek to the
	 * last good position first.
	 *
	 * The only case of a fully read file is when eof_marker
	 * is present and it is the last record in the file. If
	 * eof_marker is missing, the caller must make the
	 * decision whether to switch to the next file or not.
	 */
	fseeko(l->f, i->good_offset, SEEK_SET);
	if (fread(&magic, sizeof(magic), 1, l->f) == 1) {
		if (magic == eof_marker) {
			i->good_offset = ftello(l->f);
			i->eof_read = true;
		} else if (magic == row_marker) {
			/*
			 * Row marker at the end of a file: a sign
			 * of a corrupt log file in case of
			 * recovery, but OK in case we're in local
			 * hot standby or replication relay mode
			 * (i.e. data is being written to the
			 * file.
			 */
		} else {
			say_error("EOF marker is corrupt: %lu",
				  (unsigned long) magic);
		}
	}
	/* No more rows. */
	return 1;
}

/* }}} */

/* {{{ struct xlog */

int
xlog_close(struct xlog *l)
{
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
			xlog_sync(l);
	}

	r = fclose(l->f);
	if (r < 0)
		say_syserror("%s: close() failed", l->filename);
	free(l);
	return r;
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
		close(fileno(l->f));
		*lptr = NULL;
	}
}

static int
sync_cb(eio_req *req)
{
	int fd = (intptr_t) req->data;
	if (req->result) {
		errno = req->result;
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
	if (l->dir->sync_is_async) {
		int fd = dup(fileno(l->f));
		if (fd == -1) {
			say_syserror("%s: dup() failed", l->filename);
			return -1;
		}
		eio_fsync(fd, 0, sync_cb, (void *) (intptr_t) fd);
	} else if (fsync(fileno(l->f)) < 0) {
		say_syserror("%s: fsync failed", l->filename);
		return -1;
	}
	return 0;
}

#define SERVER_UUID_KEY "Server"
#define VCLOCK_KEY "VClock"

static int
xlog_write_meta(struct xlog *l)
{
	char *vstr = NULL;
	if (fprintf(l->f, "%s%s", l->dir->filetype, v12) < 0 ||
	    fprintf(l->f, SERVER_UUID_KEY ": %s\n",
		    tt_uuid_str(l->dir->server_uuid)) < 0 ||
	    (vstr = vclock_to_string(&l->vclock)) == NULL ||
	    fprintf(l->f, VCLOCK_KEY ": %s\n\n", vstr) < 0) {
		free(vstr);
		return -1;
	}

	return 0;
}

/**
 * Verify that file is of the given format.
 *
 * @param l		xlog object, denoting the file to check.
 * @param[out] errmsg   set if error
 *
 * @return 0 if success, -1 on error.
 */
static void
xlog_read_meta(struct xlog *l, int64_t signature)
{
	char filetype[32], version[32], buf[256];
	struct xdir *dir = l->dir;
	FILE *stream = l->f;

	if (fgets(filetype, sizeof(filetype), stream) == NULL ||
	    fgets(version, sizeof(version), stream) == NULL) {
		tnt_raise(XlogError, "%s: failed to read log file header",
			  l->filename);
	}
	if (strcmp(dir->filetype, filetype) != 0) {
		tnt_raise(XlogError, "%s: unknown filetype", l->filename);
	}

	if (strcmp(v12, version) != 0) {
		tnt_raise(XlogError, "%s: unsupported file format version",
			  l->filename);
	}
	for (;;) {
		if (fgets(buf, sizeof(buf), stream) == NULL) {
			tnt_raise(XlogError,
				  "%s: failed to read log file header",
				  l->filename);
		}
		/** Empty line indicates the end of file header. */
		if (strcmp(buf, "\n") == 0)
			break;

		/* Parse RFC822-like string */
		char *end = buf + strlen(buf);
		if (end > buf && *(end - 1) == '\n')
			*(--end) = 0; /* skip \n */
		char *key = buf;
		char *val = strchr(buf, ':');
		if (val == NULL) {
			tnt_raise(XlogError, "%s: invalid meta", l->filename);
		}
		*val++ = 0;
		while (isspace(*val))
			++val;	/* skip starting spaces */

		if (strcmp(key, SERVER_UUID_KEY) == 0) {
			if ((end - val) != UUID_STR_LEN ||
			    tt_uuid_from_string(val, &l->server_uuid) != 0) {
				tnt_raise(XlogError, "%s: can't parse node UUID",
					  l->filename);
			}
		} else if (strcmp(key, VCLOCK_KEY) == 0){
			size_t offset = vclock_from_string(&l->vclock, val);
			if (offset != 0) {
				tnt_raise(XlogError, "%s: invalid vclock at offset %zd",
					  l->filename, offset);
			}
		} else {
			/* Skip unknown key */
		}
	}

	if (!tt_uuid_is_nil(dir->server_uuid) &&
	    !tt_uuid_is_equal(dir->server_uuid, &l->server_uuid)) {
		tnt_raise(XlogError, "%s: invalid server UUID",
			  l->filename);
	}
	/*
	 * Check the match between log file name and contents:
	 * the sum of vector clock coordinates must be the same
	 * as the name of the file.
	 */
	int64_t signature_check = vclock_sum(&l->vclock);
	if (signature_check != signature) {
		tnt_raise(XlogError, "%s: signature check failed",
			  l->filename);
	}
}

struct xlog *
xlog_open_stream(struct xdir *dir, int64_t signature, FILE *file, const char *filename)
{
	/*
	 * Check fopen() result the caller first thing, to
	 * preserve the errno.
	 */
	if (file == NULL)
		tnt_raise(SystemError, "%s: failed to open file", filename);

	struct xlog *l = (struct xlog *) calloc(1, sizeof(*l));

	auto log_guard = make_scoped_guard([=]{
		fclose(file);
		free(l);
	});

	if (l == NULL)
		tnt_raise(OutOfMemory, sizeof(*l), "malloc", "struct xlog");

	l->f = file;
	snprintf(l->filename, PATH_MAX, "%s", filename);
	l->mode = LOG_READ;
	l->dir = dir;
	l->is_inprogress = false;
	l->eof_read = false;
	vclock_create(&l->vclock);

	xlog_read_meta(l, signature);

	log_guard.is_active = false;
	return l;
}

struct xlog *
xlog_open(struct xdir *dir, int64_t signature)
{
	const char *filename = format_filename(dir, signature, NONE);
	FILE *f = fopen(filename, "r");
	return xlog_open_stream(dir, signature, f, filename);
}

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
xlog_create(struct xdir *dir, const struct vclock *vclock)
{
	char *filename;
	FILE *f = NULL;
	struct xlog *l = NULL;

	int64_t signature = vclock_sum(vclock);
	assert(signature >= 0);
	assert(!tt_uuid_is_nil(dir->server_uuid));

	/*
	* Check whether a file with this name already exists.
	* We don't overwrite existing files.
	*/
	filename = format_filename(dir, signature, NONE);
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
	filename = format_filename(dir, signature, INPROGRESS);
	f = fiob_open(filename, dir->open_wflags);
	if (!f)
		goto error;
	say_info("creating `%s'", filename);
	l = (struct xlog *) calloc(1, sizeof(*l));
	if (l == NULL)
		goto error;
	l->f = f;
	snprintf(l->filename, PATH_MAX, "%s", filename);
	l->mode = LOG_WRITE;
	l->dir = dir;
	l->is_inprogress = true;
	/*  Makes no sense, but well. */
	l->eof_read = false;
	vclock_copy(&l->vclock, vclock);
	setvbuf(l->f, NULL, _IONBF, 0);
	if (xlog_write_meta(l) != 0)
		goto error;
	if (dir->suffix != INPROGRESS && xlog_rename(l))
		goto error;

	return l;
error:
	int save_errno = errno;
	say_syserror("%s: failed to open", filename);
	if (f != NULL) {
		fclose(f);
		unlink(filename); /* try to remove incomplete file */
	}
	free(l);
	errno = save_errno;
	return NULL;
}

/* }}} */

