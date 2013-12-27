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

#include "palloc.h"
#include "fiber.h"
#include "crc32.h"
#include "fio.h"
#include "tarantool_eio.h"
#include "fiob.h"

const uint32_t default_version = 11;
const log_magic_t row_marker_v11 = 0xba0babed;
const log_magic_t eof_marker_v11 = 0x10adab1e;
const char inprogress_suffix[] = ".inprogress";
const char v11[] = "0.11\n";

void
header_v11_sign(struct header_v11 *header)
{
	header->data_crc32c = crc32_calc(0, (const unsigned char *) header + sizeof(struct
						  header_v11), header->len);
	header->header_crc32c = crc32_calc(0, (const unsigned char *) &header->lsn,
					   sizeof(struct header_v11) -
					   sizeof(header->header_crc32c));
}

void
row_v11_fill(struct row_v11 *row, int64_t lsn, uint16_t tag, uint64_t cookie,
	     const char *metadata, size_t metadata_len, const char
	     *data, size_t data_len)
{
	row->marker = row_marker_v11;
	row->tag  = tag;
	row->cookie = cookie;
	memcpy(row->data, metadata, metadata_len);
	memcpy(row->data + metadata_len, data, data_len);
	header_v11_fill(&row->header, lsn, metadata_len + data_len +
			sizeof(row->tag) + sizeof(row->cookie));
}

struct log_dir snap_dir = {
	/* .panic_if_error = */ false,
	/* .sync_is_async = */ false,
	/* .open_wflags = */ 0,
	/* .filetype = */ "SNAP\n",
	/* .filename_ext = */ ".snap",
	/* .dirname = */ NULL
};

struct log_dir wal_dir = {
	/* .panic_if_error = */ false,
	/* .sync_is_async = */ true,
	/* .open_wflags = */ 0,
	/* .filetype = */ "XLOG\n",
	/* .filename_ext = */ ".xlog",
	/* .dirname = */ NULL
};

static int
cmp_i64(const void *_a, const void *_b)
{
	const int64_t *a = (const int64_t *) _a, *b = (const int64_t *) _b;
	if (*a == *b)
		return 0;
	return (*a > *b) ? 1 : -1;
}

static ssize_t
scan_dir(struct log_dir *dir, int64_t **ret_lsn)
{
	ssize_t result = -1;
	size_t i = 0, size = 1024;
	ssize_t ext_len = strlen(dir->filename_ext);
	int64_t *lsn = (int64_t *) palloc(fiber->gc_pool, sizeof(int64_t) * size);
	DIR *dh = opendir(dir->dirname);

	if (lsn == NULL || dh == NULL)
		goto out;

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

		lsn[i] = strtoll(dent->d_name, &ext, 10);
		if (strncmp(ext, dir->filename_ext, ext_len) != 0) {
			/* d_name doesn't parse entirely, ignore it */
			say_warn("can't parse `%s', skipping", dent->d_name);
			continue;
		}

		if (lsn[i] == LLONG_MAX || lsn[i] == LLONG_MIN) {
			say_warn("can't parse `%s', skipping", dent->d_name);
			continue;
		}

		i++;
		if (i == size) {
			int64_t *n = (int64_t *) palloc(fiber->gc_pool, sizeof(int64_t) * size * 2);
			if (n == NULL)
				goto out;
			memcpy(n, lsn, sizeof(int64_t) * size);
			lsn = n;
			size = size * 2;
		}
	}

	qsort(lsn, i, sizeof(int64_t), cmp_i64);

	*ret_lsn = lsn;
	result = i;
out:
	if (errno != 0)
		say_syserror("error reading directory `%s'", dir->dirname);

	if (dh != NULL)
		closedir(dh);
	return result;
}

int64_t
greatest_lsn(struct log_dir *dir)
{
	int64_t *lsn;
	ssize_t count = scan_dir(dir, &lsn);

	if (count <= 0)
		return count;

	return lsn[count - 1];
}

int64_t
find_including_file(struct log_dir *dir, int64_t target_lsn)
{
	int64_t *lsn;
	ssize_t count = scan_dir(dir, &lsn);

	if (count <= 0)
		return count;

	while (count > 1) {
		if (*lsn <= target_lsn && target_lsn < *(lsn + 1)) {
			goto out;
			return *lsn;
		}
		lsn++;
		count--;
	}

	/*
	 * we can't check here for sure will or will not last file
	 * contain record with desired lsn since number of rows in file
	 * is not known beforehand. so, we simply return the last one.
	 */

      out:
	return *lsn;
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

/* }}} */

/* {{{ struct log_io_cursor */

static const char ROW_EOF[] = "";

const char *
row_reader_v11(FILE *f, uint32_t *rowlen)
{
	struct header_v11 m;

	uint32_t header_crc, data_crc;

	if (fread(&m, sizeof(m), 1, f) != 1)
		return ROW_EOF;

	/* header crc32c calculated on <lsn, tm, len, data_crc32c> */
	header_crc = crc32_calc(0, (unsigned char *) &m + offsetof(struct header_v11, lsn),
				sizeof(m) - offsetof(struct header_v11, lsn));

	if (m.header_crc32c != header_crc) {
		say_error("header crc32c mismatch");
		return NULL;
	}
	char *row = (char *) palloc(fiber->gc_pool, sizeof(m) + m.len);
	memcpy(row, &m, sizeof(m));

	if (fread(row + sizeof(m), m.len, 1, f) != 1)
		return ROW_EOF;

	data_crc = crc32_calc(0, (unsigned char *) row + sizeof(m), m.len);
	if (m.data_crc32c != data_crc) {
		say_error("data crc32c mismatch");
		return NULL;
	}

	say_debug("read row v11 success lsn:%lld", (long long) m.lsn);
	*rowlen = m.len + sizeof(m);
	return row;
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
	prelease(fiber->gc_pool);
}

/**
 * Read logfile contents using designated format, panic if
 * the log is corrupted/unreadable.
 *
 * @param i	iterator object, encapsulating log specifics.
 *
 */
const char *
log_io_cursor_next(struct log_io_cursor *i, uint32_t *rowlen)
{
	struct log_io *l = i->log;
	const char *row;
	log_magic_t magic;
	off_t marker_offset = 0;

	assert(i->eof_read == false);

	say_debug("log_io_cursor_next: marker:0x%016X/%zu",
		  row_marker_v11, sizeof(row_marker_v11));

	/*
	 * Don't let gc pool grow too much. Yet to
	 * it before reading the next row, to make
	 * sure it's not freed along here.
	 */
	prelease_after(fiber->gc_pool, 128 * 1024);

restart:
	if (marker_offset > 0)
		fseeko(l->f, marker_offset + 1, SEEK_SET);

	if (fread(&magic, sizeof(magic), 1, l->f) != 1)
		goto eof;

	while (magic != row_marker_v11) {
		int c = fgetc(l->f);
		if (c == EOF) {
			say_debug("eof while looking for magic");
			goto eof;
		}
		magic = magic >> 8 |
			((log_magic_t) c & 0xff) << (sizeof(magic)*8 - 8);
	}
	marker_offset = ftello(l->f) - sizeof(row_marker_v11);
	if (i->good_offset != marker_offset)
		say_warn("skipped %jd bytes after 0x%08jx offset",
			(intmax_t)(marker_offset - i->good_offset),
			(uintmax_t)i->good_offset);
	say_debug("magic found at 0x%08jx", (uintmax_t)marker_offset);

	row = row_reader_v11(l->f, rowlen);
	if (row == ROW_EOF)
		goto eof;

	if (row == NULL) {
		if (l->dir->panic_if_error)
			panic("failed to read row");
		say_warn("failed to read row");
		goto restart;
	}

	i->good_offset = ftello(l->f);
	i->row_count++;

	if (i->row_count % 100000 == 0)
		say_info("%.1fM rows processed", i->row_count / 1000000.);

	return row;
eof:
	/*
	 * The only two cases of fully read file:
	 * 1. sizeof(eof_marker) > 0 and it is the last record in file
	 * 2. sizeof(eof_marker) == 0 and there is no unread data in file
	 */
	if (ftello(l->f) == i->good_offset + sizeof(eof_marker_v11)) {
		fseeko(l->f, i->good_offset, SEEK_SET);
		if (fread(&magic, sizeof(magic), 1, l->f) != 1) {

			say_error("can't read eof marker");
		} else if (magic == eof_marker_v11) {
			i->good_offset = ftello(l->f);
			i->eof_read = true;
		} else if (magic != row_marker_v11) {
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
	return NULL;
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
		fwrite(&eof_marker_v11, 1, sizeof(log_magic_t), l->f);
		/*
		 * Sync the file before closing, since
		 * otherwise we can end up with a partially
		 * written file in case of a crash.
		 * Do not sync if the file is opened with O_SYNC.
		 */
		if (! (l->dir->open_wflags & WAL_SYNC_FLAG))
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

static int
log_io_write_header(struct log_io *l)
{
	int ret = fprintf(l->f, "%s%s\n", l->dir->filetype, v11);

	return ret < 0 ? -1 : 0;
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
log_io_verify_meta(struct log_io *l, const char **errmsg)
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

	if (strcmp(v11, version) != 0) {
		*errmsg = "unknown version";
		goto error;
	}
	for (;;) {
		if (fgets(buf, sizeof(buf), stream) == NULL) {
			*errmsg = "failed to read log file header";
			goto error;
		}
		if (strcmp(buf, "\n") == 0 || strcmp(buf, "\r\n") == 0)
			break;
	}
	return 0;
error:
	return -1;
}

struct log_io *
log_io_open(struct log_dir *dir, enum log_mode mode,
	    const char *filename, enum log_suffix suffix, FILE *file)
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
		if (log_io_verify_meta(l, &errmsg) != 0) {
			errmsg = strerror(errno);
			goto error;
		}
	} else { /* LOG_WRITE */
		setvbuf(l->f, NULL, _IONBF, 0);
		if (log_io_write_header(l) != 0) {
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
log_io_open_for_read(struct log_dir *dir, int64_t lsn, enum log_suffix suffix)
{
	assert(lsn != 0);

	const char *filename = format_filename(dir, lsn, suffix);
	FILE *f = fopen(filename, "r");
	return log_io_open(dir, LOG_READ, filename, suffix, f);
}

/**
 * In case of error, writes a message to the server log
 * and sets errno.
 */
struct log_io *
log_io_open_for_write(struct log_dir *dir, int64_t lsn, enum log_suffix suffix)
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

	/** TODO: use here dir->open_wflags */
	f = fiob_open(filename, "wx");

	if (!f)
		goto error;
	say_info("creating `%s'", filename);
	return log_io_open(dir, LOG_WRITE, filename, suffix, f);
error:
	say_syserror("%s: failed to open `%s'", __func__, filename);
	return NULL;
}

/* }}} */

