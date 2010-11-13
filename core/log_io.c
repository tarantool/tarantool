/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <fiber.h>
#include <log_io.h>
#include <palloc.h>
#include <say.h>
#include <third_party/crc32.h>
#include <util.h>
#include <pickle.h>
#include <tbuf.h>

const u16 default_tag = 0;
const u32 default_version = 11;
const u32 snap_marker_v04 = -1U;
const u64 xlog_marker_v04 = -1ULL;
const u64 xlog_eof_marker_v04 = 0;
const u32 marker_v11 = 0xba0babed;
const u32 eof_marker_v11 = 0x10adab1e;
const char *snap_suffix = ".snap";
const char *xlog_suffix = ".xlog";
const char *v04 = "0.04\n";
const char *v03 = "0.03\n";
const char *v11 = "0.11\n";
const char *snap_mark = "SNAP\n";
const char *xlog_mark = "XLOG\n";

#define ROW_EOF (void *)1

static struct tbuf *row_reader_v04(FILE *f, struct palloc_pool *pool);
static struct tbuf *row_reader_v11(FILE *f, struct palloc_pool *pool);

struct log_io_iter {
	struct tarantool_coro coro;
	struct log_io *log;
	void *from;
	void *to;
	int error;
	bool eof;
	int io_rate_limit;
};

struct row_v04 {
	i64 lsn;		/* this used to be tid */
	u16 type;
	u32 len;
	u8 data[];
} __packed__;

static inline struct row_v04 *row_v04(const struct tbuf *t)
{
	return (struct row_v04 *)t->data;
}


int
confirm_lsn(struct recovery_state *r, i64 lsn)
{
	assert(r->confirmed_lsn <= r->lsn);

	if (r->confirmed_lsn < lsn) {
		if (r->confirmed_lsn + 1 != lsn)
			say_warn("non consecutive lsn, last confirmed:%" PRIi64
				 " new:%" PRIi64 " diff: %" PRIi64,
				 r->confirmed_lsn, lsn, lsn - r->confirmed_lsn);
		r->confirmed_lsn = lsn;
		return 0;
	} else {
		say_warn("lsn double confirmed:%" PRIi64, r->confirmed_lsn);
	}

	return -1;
}

i64
next_lsn(struct recovery_state *r, i64 new_lsn)
{
	if (new_lsn == 0)
		r->lsn++;
	else
		r->lsn = new_lsn;

	say_debug("next_lsn(%p, %" PRIi64 ") => %" PRIi64, r, new_lsn, r->lsn);
	return r->lsn;
}

static void
xlog04_class(struct log_io_class *c)
{
	c->suffix = xlog_suffix;
	c->filetype = xlog_mark;
	c->version = v04;
	c->reader = row_reader_v04;
	c->marker = xlog_marker_v04;
	c->marker_size = sizeof(xlog_marker_v04);
	c->eof_marker = xlog_eof_marker_v04;
	c->eof_marker_size = sizeof(xlog_eof_marker_v04);

	c->rows_per_file = 50000;	/* sane defaults */
	c->fsync_delay = 0;
}

static void
v11_class(struct log_io_class *c)
{
	c->suffix = xlog_suffix;
	c->filetype = xlog_mark;
	c->version = v11;
	c->reader = row_reader_v11;
	c->marker = marker_v11;
	c->marker_size = sizeof(marker_v11);
	c->eof_marker = eof_marker_v11;
	c->eof_marker_size = sizeof(eof_marker_v11);

	c->fsync_delay = 0;
}

static struct log_io_class **
snap_classes(row_reader snap_row_reader, const char *dirname)
{
	struct log_io_class **c = calloc(3, sizeof(*c));
	if (c == NULL)
		panic("calloc");

	c[0] = calloc(1, sizeof(**c));
	c[1] = calloc(1, sizeof(**c));
	if (c[0] == NULL || c[1] == NULL)
		panic("calloc");

	c[0]->suffix = snap_suffix;
	c[0]->filetype = snap_mark;
	c[0]->version = v03;
	c[0]->eof_marker_size = 0;	/* no end marker */
	c[0]->marker = snap_marker_v04;
	c[0]->marker_size = sizeof(snap_marker_v04);
	c[0]->rows_per_file = 0;
	c[0]->reader = snap_row_reader;

	v11_class(c[1]);
	c[1]->filetype = c[0]->filetype;
	c[1]->suffix = c[0]->suffix;

	c[0]->dirname = c[1]->dirname = dirname;
	return c;
}

static struct log_io_class **
xlog_classes(const char *dirname)
{
	struct log_io_class **c = calloc(3, sizeof(*c));
	if (c == NULL)
		panic("calloc");

	c[0] = calloc(1, sizeof(**c));
	c[1] = calloc(1, sizeof(**c));
	if (c[0] == NULL || c[1] == NULL)
		panic("calloc");

	xlog04_class(c[0]);
	v11_class(c[1]);

	c[0]->dirname = c[1]->dirname = dirname;
	return c;
}

static void *
iter_inner(struct log_io_iter *i, void *data)
{
	i->to = data;
	coro_transfer(&fiber->coro.ctx, &i->coro.ctx);
	return i->from;
}

static void *
iter_outer(struct log_io_iter *i, void *data)
{
	i->from = data;
	coro_transfer(&i->coro.ctx, &fiber->coro.ctx);
	return i->to;
}

static void
close_iter(struct log_io_iter *i)
{
	tarantool_coro_destroy(&i->coro);
}

static void
read_rows(struct log_io_iter *i)
{
	struct log_io *l = i->log;
	struct tbuf *row;
	u64 magic;
	off_t marker_offset = 0, good_offset;
	const u64 marker_mask = (u64)-1 >> ((sizeof(u64) - l->class->marker_size) * 8);
	int row_count = 0;
	int error = 0;
	int eof = 0;

	say_debug("read_rows: marker:0x%016" PRIX64 "/%" PRI_SZ,
		  l->class->marker, l->class->marker_size);

	good_offset = ftello(l->f);
      restart:
	if (marker_offset > 0)
		fseeko(l->f, marker_offset + 1, SEEK_SET);

	for (;;) {
		say_debug("read_rows: loop start offt 0x%08" PRI_XFFT, ftello(l->f));
		if (fread(&magic, l->class->marker_size, 1, l->f) != 1)
			goto eof;

		while ((magic & marker_mask) != l->class->marker) {
			int c = fgetc(l->f);
			if (c == EOF) {
				say_debug("eof while looking for magic");
				goto eof;
			}
			magic >>= 8;
			magic |= (((u64)c & 0xff) << ((l->class->marker_size - 1) * 8));
		}
		marker_offset = ftello(l->f) - l->class->marker_size;
		if (good_offset != marker_offset)
			say_warn("skipped %" PRI_OFFT " bytes after 0x%08" PRI_XFFT " offset",
				 marker_offset - good_offset, good_offset);
		say_debug("magic found at 0x%08" PRI_XFFT, marker_offset);

		row = l->class->reader(l->f, fiber->pool);
		if (row == ROW_EOF)
			goto eof;

		if (row == NULL) {
			if (l->class->panic_if_error)
				panic("failed to read row");
			say_warn("failed to read row");
			goto restart;
		}

		good_offset = ftello(l->f);

		if (!iter_outer(i, row)) {
			error = -1;
			goto out;
		}

		prelease_after(fiber->pool, 128 * 1024);

		if (++row_count % 100000 == 0)
			say_info("%.1fM rows processed", row_count / 1000000.);
	}
      eof:
	/*
	 * then only two cases of fully read file:
	 * 1. eof_marker_size > 0 and it is the last record in file
	 * 2. eof_marker_size == 0 and there is no unread data in file
	 */
	if (l->class->eof_marker_size > 0 &&
	    ftello(l->f) == good_offset + l->class->eof_marker_size) {
		fseeko(l->f, good_offset, SEEK_SET);
		if (fread(&magic, l->class->eof_marker_size, 1, l->f) != 1) {
			say_error("can't read eof marker");
			goto out;
		}
		if (memcmp(&magic, &l->class->eof_marker, l->class->eof_marker_size) != 0)
			goto out;

		good_offset = ftello(l->f);
		eof = 1;
		goto out;
	}
	if (l->class->eof_marker_size == 0 && ftello(l->f) == good_offset) {
		eof = 1;
		goto out;
	}

      out:
	l->rows += row_count;

	fseeko(l->f, good_offset, SEEK_SET);	/* seek back to last known good offset */
	prelease(fiber->pool);

	if (error)
		i->error = error;
	if (eof)
		i->eof = eof;

	iter_outer(i, NULL);
}

static void
iter_open(struct log_io *l, struct log_io_iter *i, void (*iterator) (struct log_io_iter * i))
{
	memset(i, 0, sizeof(*i));
	i->log = l;
	tarantool_coro_create(&i->coro, (void *)iterator, i);
}

static int
cmp_i64(const void *_a, const void *_b)
{
	const i64 *a = _a, *b = _b;
	if (*a == *b)
		return 0;
	return (*a > *b) ? 1 : -1;
}

static ssize_t
scan_dir(struct log_io_class *class, i64 **ret_lsn)
{
	DIR *dh = NULL;
	struct dirent *dent;
	i64 *lsn;
	size_t suffix_len, i = 0, size = 1024;
	char *parse_suffix;
	ssize_t result = -1;

	dh = opendir(class->dirname);
	if (dh == NULL)
		goto out;

	suffix_len = strlen(class->suffix);

	lsn = palloc(fiber->pool, sizeof(i64) * size);
	if (lsn == NULL)
		goto out;

	errno = 0;
	while ((dent = readdir(dh)) != NULL) {
		size_t len = strlen(dent->d_name) + 1;
		if (len < suffix_len + 1)
			continue;
		if (strcmp(dent->d_name + len - 1 - suffix_len, class->suffix))
			continue;

		lsn[i] = strtoll(dent->d_name, &parse_suffix, 10);
		if (strcmp(parse_suffix, class->suffix) != 0) {	/* d_name doesn't parse entirely, ignore it */
			say_warn("can't parse `%s', skipping", dent->d_name);
			continue;
		}

		if (lsn[i] == LLONG_MAX || lsn[i] == LLONG_MIN) {
			say_warn("can't parse `%s', skipping", dent->d_name);
			continue;
		}

		i++;
		if (i == size) {
			i64 *n = palloc(fiber->pool, sizeof(i64) * size * 2);
			if (n == NULL)
				goto out;
			memcpy(n, lsn, sizeof(i64) * size);
			lsn = n;
			size = size * 2;
		}
	}

	qsort(lsn, i, sizeof(i64), cmp_i64);

	*ret_lsn = lsn;
	result = i;
      out:
	if (errno != 0)
		say_syserror("error reading directory `%s'", class->dirname);

	if (dh != NULL)
		closedir(dh);
	return result;
}

static i64
greatest_lsn(struct log_io_class *class)
{
	i64 *lsn;
	ssize_t count = scan_dir(class, &lsn);

	if (count <= 0)
		return count;

	return lsn[count - 1];
}

static i64
find_including_file(struct log_io_class *class, i64 target_lsn)
{
	i64 *lsn;
	ssize_t count = scan_dir(class, &lsn);

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

struct tbuf *
convert_to_v11(struct tbuf *orig, i64 lsn)
{
	struct tbuf *row = tbuf_alloc(orig->pool);
	tbuf_ensure(row, sizeof(struct row_v11));
	row->len = sizeof(struct row_v11);
	row_v11(row)->lsn = lsn;
	row_v11(row)->tm = 0;
	row_v11(row)->len = orig->len + sizeof(default_tag);

	tbuf_append(row, &default_tag, sizeof(default_tag));
	tbuf_append(row, orig->data, orig->len);
	return row;
}

static struct tbuf *
row_reader_v04(FILE *f, struct palloc_pool *pool)
{
	const int header_size = offsetof(struct row_v04, data);
	struct tbuf *m = tbuf_alloc(pool);
	u32 crc, calculated_crc;

	/*
	 * it's imposible to distinguish between EOF and bad record condition here
	 * since bad record may have bogus length
	 * so if record is suspicious simply return NULL to the caller
	 */

	if (fread(m->data, header_size, 1, f) != 1)
		return ROW_EOF;
	m->len = header_size;

	/* filter out rows with definitly wrong length */
	if (row_v04(m)->len > (1 << 20)) {
		say_error("record too long(%" PRIi32 "), probably damaged", row_v04(m)->len);
		return NULL;
	}

	tbuf_ensure(m, header_size + row_v04(m)->len);
	if (fread(row_v04(m)->data, row_v04(m)->len, 1, f) != 1)
		return ROW_EOF;

	m->len += row_v04(m)->len;

	if (fread(&crc, sizeof(crc), 1, f) != 1)
		return ROW_EOF;

	calculated_crc = crc32(m->data, m->len);
	if (crc != calculated_crc) {
		say_error("crc32 mismatch");
		return NULL;
	}

	say_debug("read row v04 success lsn:%" PRIi64, row_v04(m)->lsn);

	/* we're copying row data twice here, it's ok since this is legacy function */
	struct tbuf *data = tbuf_alloc(pool);
	tbuf_append(data, &row_v04(m)->type, sizeof(row_v04(m)->type));
	tbuf_append(data, row_v04(m)->data, row_v04(m)->len);

	return convert_to_v11(data, row_v04(m)->lsn);
}

static struct tbuf *
row_reader_v11(FILE *f, struct palloc_pool *pool)
{
	struct tbuf *m = tbuf_alloc(pool);

	u32 header_crc, data_crc;

	tbuf_ensure(m, sizeof(struct row_v11));
	if (fread(m->data, sizeof(struct row_v11), 1, f) != 1)
		return ROW_EOF;

	m->len = offsetof(struct row_v11, data);

	/* header crc32c calculated on <lsn, tm, len, data_crc32c> */
	header_crc = crc32c(0, m->data + offsetof(struct row_v11, lsn),
			    sizeof(struct row_v11) - offsetof(struct row_v11, lsn));

	if (row_v11(m)->header_crc32c != header_crc) {
		say_error("header crc32c mismatch");
		return NULL;
	}

	tbuf_ensure(m, m->len + row_v11(m)->len);
	if (fread(row_v11(m)->data, row_v11(m)->len, 1, f) != 1)
		return ROW_EOF;

	m->len += row_v11(m)->len;

	data_crc = crc32c(0, row_v11(m)->data, row_v11(m)->len);
	if (row_v11(m)->data_crc32c != data_crc) {
		say_error("data crc32c mismatch");
		return NULL;
	}

	say_debug("read row v11 success lsn:%" PRIi64, row_v11(m)->lsn);
	return m;
}

int
close_log(struct log_io **lptr)
{
	struct log_io *l = *lptr;
	int r;

	if (l->class->eof_marker_size > 0 && l->mode == LOG_WRITE) {
		if (fwrite(&l->class->eof_marker, l->class->eof_marker_size, 1, l->f) != 1)
			say_error("can't write eof_marker");
	}

	if (ev_is_active(&l->stat))
		ev_stat_stop(&l->stat);
	r = fclose(l->f);
	if (r < 0)
		say_error("can't close");
	free(l);
	*lptr = NULL;
	return r;
}

static int
flush_log(struct log_io *l)
{

	static double last = 0;
	double now;
	struct timeval t;

	if (fflush(l->f) < 0)
		return -1;

	if (gettimeofday(&t, NULL) < 0) {
		say_syserror("gettimeofday");
		return -1;
	}
	now = t.tv_sec + t.tv_usec / 1000000.;

	if (l->class->fsync_delay == 0 || now - last < l->class->fsync_delay)
		return 0;
#ifdef Linux
	if (fdatasync(fileno(l->f)) < 0) {
		say_syserror("fdatasync");
		return -1;
	}
#else
	if (fsync(fileno(l->f)) < 0) {
		say_syserror("fsync");
		return -1;
	}
#endif
	last = now;
	return 0;
}

static int
write_header(struct log_io *l)
{
	char buf[27];
	time_t tm;

	if (fwrite(l->class->filetype, strlen(l->class->filetype), 1, l->f) != 1)
		return -1;

	if (fwrite(l->class->version, strlen(l->class->version), 1, l->f) != 1)
		return -1;

	if (strcmp(l->class->version, v11) == 0) {
		if (fwrite("\n", 1, 1, l->f) != 1)
			return -1;
	} else {
		time(&tm);
		ctime_r(&tm, buf);
		if (fwrite(buf, strlen(buf), 1, l->f) != 1)
			return -1;
	}

	return 0;
}

static char *
format_filename(char *filename, struct log_io_class *class, i64 lsn, int suffix)
{
	static char buf[PATH_MAX + 1];

	if (filename == NULL)
		filename = buf;

	switch (suffix) {
	case 0:
		snprintf(filename, PATH_MAX, "%s/%020" PRIi64 "%s",
			 class->dirname, lsn, class->suffix);
		break;
	case -1:
		snprintf(filename, PATH_MAX, "%s/%020" PRIi64 "%s.inprogress",
			 class->dirname, lsn, class->suffix);
		break;
	default:
		snprintf(filename, PATH_MAX, "%s/%020" PRIi64 "%s.%i",
			 class->dirname, lsn, class->suffix, suffix);
		break;
	}
	return filename;
}

static struct log_io *
open_for_read(struct recovery_state *recover, struct log_io_class **class, i64 lsn, int suffix,
	      const char *filename)
{
	char filetype[32], version[32], buf[256];
	struct log_io *l = NULL;
	char *r;
	char *error = "unknown error";

	l = malloc(sizeof(*l));
	if (l == NULL)
		goto error;
	memset(l, 0, sizeof(*l));
	l->mode = LOG_READ;
	l->stat.data = recover;

	/* when filename is not null it is forced open for debug reading */
	if (filename == NULL) {
		assert(lsn != 0);
		format_filename(l->filename, *class, lsn, suffix);
	} else {
		assert(lsn == 0);
		strncpy(l->filename, filename, PATH_MAX);
	}

	say_debug("find_log for reading `%s'", l->filename);

	l->f = fopen(l->filename, "r");
	if (l->f == NULL) {
		error = strerror(errno);
		goto error;
	}

	r = fgets(filetype, sizeof(filetype), l->f);
	if (r == NULL) {
		error = "header reading failed";
		goto error;
	}

	r = fgets(version, sizeof(version), l->f);
	if (r == NULL) {
		error = "header reading failed";
		goto error;
	}

	if (strcmp((*class)->filetype, filetype) != 0) {
		error = "unknown filetype";
		goto error;
	}

	while (*class) {
		if (strcmp((*class)->version, version) == 0)
			break;
		class++;
	}

	if (*class == NULL) {
		error = "unknown version";
		goto error;
	}
	l->class = *class;

	if (strcmp(version, v11) == 0) {
		for (;;) {
			r = fgets(buf, sizeof(buf), l->f);
			if (r == NULL) {
				error = "header reading failed";
				goto error;
			}
			if (strcmp(r, "\n") == 0 || strcmp(r, "\r\n") == 0)
				break;
		}
	} else {
		r = fgets(buf, sizeof(buf), l->f);	/* skip line with time */
		if (r == NULL) {
			error = "header reading failed";
			goto error;
		}
	}

	return l;
      error:
	say_error("open_for_read: failed to open `%s': %s", l->filename, error);
	if (l != NULL) {
		if (l->f != NULL)
			fclose(l->f);
		free(l);
	}
	return NULL;
}

struct log_io *
open_for_write(struct recovery_state *recover, struct log_io_class *class, i64 lsn, int suffix)
{
	struct log_io *l = NULL;
	int fd;
	char *error = "unknown error";

	l = malloc(sizeof(*l));
	if (l == NULL)
		goto error;
	memset(l, 0, sizeof(*l));
	l->mode = LOG_WRITE;
	l->class = class;
	l->stat.data = recover;

	assert(lsn > 0);

	format_filename(l->filename, class, lsn, suffix);
	say_debug("find_log for writing `%s'", l->filename);

	fd = open(l->filename, O_WRONLY | O_CREAT | O_EXCL | O_APPEND, 0664);
	if (fd < 0) {
		error = strerror(errno);
		goto error;
	}

	l->f = fdopen(fd, "a");
	if (l->f == NULL) {
		error = strerror(errno);
		goto error;
	}

	say_info("creating `%s'", l->filename);
	write_header(l);
	return l;
      error:
	say_error("find_log: failed to open `%s': %s", l->filename, error);
	if (l != NULL) {
		if (l->f != NULL)
			fclose(l->f);
		free(l);
	}
	return NULL;
}

/* this little hole shouldn't be used too much */
int
read_log(const char *filename, row_reader reader,
	 row_handler *xlog_handler, row_handler *snap_handler, void *state)
{
	struct log_io_iter i;
	struct log_io *l;
	struct log_io_class **c;
	struct tbuf *row;
	row_handler *h;

	if (strstr(filename, xlog_suffix)) {
		c = xlog_classes(NULL);
		h = xlog_handler;
	} else if (strstr(filename, snap_suffix)) {
		c = snap_classes(reader, NULL);
		h = snap_handler;
	} else {
		say_error("don't know what how to read `%s'", filename);
		return -1;
	}

	l = open_for_read(NULL, c, 0, 0, filename);
	iter_open(l, &i, read_rows);
	while ((row = iter_inner(&i, (void *)1)))
		h(state, row);

	if (i.error != 0)
		say_error("binary log `%s' wasn't correctly closed", filename);

	close_iter(&i);
	return i.error;
}

static int
recover_snap(struct recovery_state *r)
{
	struct log_io_iter i;
	struct log_io *snap = NULL;
	struct tbuf *row;
	int result = -1;
	i64 lsn;

	memset(&i, 0, sizeof(i));

	if (setjmp(fiber->exc) != 0) {
		result = -1;
		goto out;
	}

	lsn = greatest_lsn(r->snap_prefered_class);

	if (lsn <= 0) {
		say_error("can't find snapshot");
		goto out;
	}

	snap = open_for_read(r, r->snap_class, lsn, 0, NULL);
	if (snap == NULL) {
		say_error("can't find/open snapshot");
		goto out;
	}

	iter_open(snap, &i, read_rows);
	say_info("recover from `%s'", snap->filename);

	while ((row = iter_inner(&i, (void *)1))) {
		if (r->snap_row_handler(r, row) < 0) {
			result = -1;
			goto out;
		}
	}
	result = i.error;
	if (result == 0)
		r->lsn = r->confirmed_lsn = lsn;
      out:
	if (result != 0)
		say_error("failure reading snapshot");

	if (i.log != NULL)
		close_iter(&i);

	if (snap != NULL)
		close_log(&snap);

	prelease(fiber->pool);
	return result;
}

/*
 * return value:
 * -1: error
 * 0: eof
 * 1: ok, maybe read something
 */

#define LOG_EOF 0

static int
recover_wal(struct recovery_state *r, struct log_io *l)
{
	struct log_io_iter i;
	struct tbuf *row;
	int result;

	memset(&i, 0, sizeof(i));
	iter_open(l, &i, read_rows);

	while ((row = iter_inner(&i, (void *)1))) {
		i64 lsn = row_v11(row)->lsn;
		if (r && lsn <= r->confirmed_lsn) {
			say_debug("skipping too young row");
			continue;
		}

		/*  after handler(r, row) returned, row may be modified, do not use it */
		if (r->wal_row_handler(r, row) < 0) {
			say_error("row_handler returned error");
			result = -1;
			goto out;
		}

		if (r) {
			next_lsn(r, lsn);
			confirm_lsn(r, lsn);
		}
	}
	result = i.error;
      out:
	/*
	 * since we don't close log_io
	 * we must rewind log_io to last known
	 * good position if where was error
	 */
	if (row)
		iter_inner(&i, NULL);

	if (result == 0) {
		if (i.eof)
			result = LOG_EOF;
		else
			result = 1;
	}

	close_iter(&i);
	prelease(fiber->pool);

	return result;
}

/*
 * this function will not close r->current_wal if recovery was successful
 */
static int
recover_remaining_wals(struct recovery_state *r)
{
	int result = 0;
	struct log_io *next_wal;
	char *name;
	int suffix = 0;
	i64 current_lsn, wal_greatest_lsn;
	size_t rows_before;

	current_lsn = r->confirmed_lsn + 1;
	wal_greatest_lsn = greatest_lsn(r->wal_prefered_class);

	/* if the caller already opened WAL for us, recover from it first */
	if (r->current_wal != NULL)
		goto recover_current_wal;

	while (r->confirmed_lsn < wal_greatest_lsn) {
		/* if newer WAL appeared in directory before current_wal was fully read try reread last */
		if (r->current_wal != NULL) {
			if (r->current_wal->retry++ < 3) {
				say_warn("try reread `%s' despite newer WAL exists",
					 r->current_wal->filename);
				goto recover_current_wal;
			} else {
				say_warn("wal `%s' wasn't correctly closed",
					 r->current_wal->filename);
				close_log(&r->current_wal);
			}
		}

		current_lsn = r->confirmed_lsn + 1;	/* TODO: find better way looking for next xlog */
		next_wal = open_for_read(r, r->wal_class, current_lsn, suffix, NULL);
		if (next_wal == NULL) {
			if (suffix++ < 10)
				continue;
			result = 0;
			break;
		}
		assert(r->current_wal == NULL);
		r->current_wal = next_wal;
		say_info("recover from `%s'", r->current_wal->filename);

	      recover_current_wal:
		rows_before = r->current_wal->rows;
		result = recover_wal(r, r->current_wal);
		if (result < 0) {
			say_error("failure reading from %s", r->current_wal->filename);
			break;
		}

		if (r->current_wal->rows > 0 && r->current_wal->rows != rows_before)
			r->current_wal->retry = 0;

		/*
		 * rows == 0 could possible indicate an filename confilct
		 * retry filename with same lsn but with bigger suffix
		 */
		if (r->current_wal->rows == 0) {
			say_error("read zero records from %s, RETRY", r->current_wal->filename);
			if (suffix++ < 10)
				continue;

			say_error("too many filename conflicters");
			result = -1;
			break;
		} else {
			name = format_filename(NULL, r->wal_prefered_class,
					       current_lsn, suffix + 1);
			if (access(name, F_OK) == 0) {
				say_error("found conflicter `%s' after successful reading", name);
				result = -1;
				break;
			}
		}

		if (result == LOG_EOF) {
			say_info("done `%s' confirmed_lsn:%" PRIi64, r->current_wal->filename,
				 r->confirmed_lsn);
			close_log(&r->current_wal);
		}
		suffix = 0;
	}

	/*
	 * it's not a fatal error then last wal is empty
	 * but if we lost some logs it is fatal error
	 */
	if (wal_greatest_lsn > r->confirmed_lsn + 1) {
		say_error("not all wals have been successfuly read");
		result = -1;
	}

	prelease(fiber->pool);
	return result;
}

int
recover(struct recovery_state *r, i64 lsn)
{
	int result = -1;

	/*
	 * if caller set confirmed_lsn to non zero value, snapshot recovery
	 * will be skipped, but wal reading still happens
	 */

	say_info("recovery start");
	if (lsn == 0) {
		result = recover_snap(r);
		if (result < 0) {
			if (greatest_lsn(r->snap_prefered_class) <= 0) {
				say_crit("don't you forget to initialize storage with --init_storage switch?");
				_exit(1);
			}
			panic("snapshot recovery failed");
		}
		say_info("snapshot recovered, confirmed lsn:%" PRIi64, r->confirmed_lsn);
	} else {
		/*
		 * note, that recovery start with lsn _NEXT_ to confirmed one
		 */
		r->lsn = r->confirmed_lsn = lsn - 1;
	}

	/*
	 * just after snapshot recovery current_wal isn't known
	 * so find wal which contains record with next lsn
	 */
	if (r->current_wal == NULL) {
		i64 next_lsn = r->confirmed_lsn + 1;
		i64 lsn = find_including_file(r->wal_prefered_class, next_lsn);
		if (lsn <= 0) {
			say_error("can't find wal containing record with lsn:%" PRIi64, next_lsn);
			result = -1;
			goto out;
		} else {
			r->current_wal = open_for_read(r, r->wal_class, lsn, 0, NULL);
			if (r->current_wal == NULL) {
				result = -1;
				goto out;
			}
		}
	}

	result = recover_remaining_wals(r);
	if (result < 0)
		panic("recover failed");
	say_info("wals recovered, confirmed lsn: %" PRIi64, r->confirmed_lsn);
      out:
	prelease(fiber->pool);
	return result;
}

static void recover_follow_file(ev_stat *w, int revents __unused__);

static void
recover_follow_dir(ev_timer *w, int revents __unused__)
{
	struct recovery_state *r = w->data;
	struct log_io *wal = r->current_wal;
	int result = recover_remaining_wals(r);
	if (result < 0)
		panic("recover failed: %i", result);

	/* recover_remaining_wals found new wal */
	if (r->current_wal != NULL && wal != r->current_wal) {
		ev_stat *stat = &r->current_wal->stat;
		ev_stat_init(stat, recover_follow_file, r->current_wal->filename, 0.);
		ev_stat_start(stat);
	}
}

static void
recover_follow_file(ev_stat *w, int revents __unused__)
{
	struct recovery_state *r = w->data;
	int result;
	result = recover_wal(r, r->current_wal);
	if (result < 0)
		panic("recover failed");
	if (result == LOG_EOF) {
		say_info("done `%s' confirmed_lsn:%" PRIi64, r->current_wal->filename,
			 r->confirmed_lsn);
		close_log(&r->current_wal);
		recover_follow_dir((ev_timer *)w, 0);
	}
}

void
recover_follow(struct recovery_state *r, ev_tstamp wal_dir_rescan_delay)
{
	ev_timer_init(&r->wal_timer, recover_follow_dir,
		      wal_dir_rescan_delay, wal_dir_rescan_delay);
	ev_timer_start(&r->wal_timer);
	if (r->current_wal != NULL) {
		ev_stat *stat = &r->current_wal->stat;
		ev_stat_init(stat, recover_follow_file, r->current_wal->filename, 0.);
		ev_stat_start(stat);
	}
}

void
recover_finalize(struct recovery_state *r)
{
	int result;

	if (ev_is_active(&r->wal_timer))
		ev_timer_stop(&r->wal_timer);

	if (r->current_wal != NULL) {
		if (ev_is_active(&r->current_wal->stat))
			ev_stat_stop(&r->current_wal->stat);
	}

	result = recover_remaining_wals(r);
	if (result < 0)
		panic("unable to scucessfully finalize recovery");

	if (r->current_wal != NULL && result != LOG_EOF) {
		say_warn("wal `%s' wasn't correctly closed", r->current_wal->filename);
		close_log(&r->current_wal);
	}
}

static struct wal_write_request *
wal_write_request(const struct tbuf *t)
{
	return t->data;
}

static struct tbuf *
write_to_disk(void *_state, struct tbuf *t)
{
	static struct log_io *wal = NULL, *wal_to_close = NULL;
	static size_t rows = 0;
	struct tbuf *reply, *header;
	struct recovery_state *r = _state;
	u32 result = 0;
	int suffix = 0;

	/* we're not running inside ev_loop, so update ev_now manually */
	ev_now_update();

	/* caller requested termination */
	if (t == NULL) {
		if (wal != NULL)
			close_log(&wal);
		return NULL;
	}

	reply = tbuf_alloc(t->pool);

	/* if there is filename conflict, try filename with lager suffix */
	while (wal == NULL && suffix < 10) {
		wal = open_for_write(r, r->wal_prefered_class, wal_write_request(t)->lsn, suffix);
		suffix++;
	}
	if (wal_to_close != NULL) {
		if (close_log(&wal_to_close) != 0)
			goto fail;
	}
	if (wal == NULL) {
		say_syserror("can't open wal");
		goto fail;
	}
	if (fwrite(&wal->class->marker, wal->class->marker_size, 1, wal->f) != 1) {
		say_syserror("can't write marker to wal");
		goto fail;
	}

	header = tbuf_alloc(t->pool);
	tbuf_ensure(header, sizeof(struct row_v11));
	header->len = sizeof(struct row_v11);

	row_v11(header)->lsn = wal_write_request(t)->lsn;
	row_v11(header)->tm = ev_now();
	row_v11(header)->len = wal_write_request(t)->len;
	row_v11(header)->data_crc32c =
		crc32c(0, wal_write_request(t)->data, wal_write_request(t)->len);
	row_v11(header)->header_crc32c =
		crc32c(0, header->data + field_sizeof(struct row_v11, header_crc32c),
		       sizeof(struct row_v11) - field_sizeof(struct row_v11, header_crc32c));

	if (fwrite(header->data, header->len, 1, wal->f) != 1) {
		say_syserror("can't write row header to wal");
		goto fail;
	}

	if (fwrite(wal_write_request(t)->data, wal_write_request(t)->len, 1, wal->f) != 1) {
		say_syserror("can't write row data to wal");
		goto fail;
	}

	if (flush_log(wal) < 0) {
		say_syserror("can't flush wal");
		goto fail;
	}

	rows++;
	if (wal->class->rows_per_file <= rows ||
	    (wal_write_request(t)->lsn + 1) % wal->class->rows_per_file == 0) {
		wal_to_close = wal;
		wal = NULL;
		rows = 0;
		suffix = 0;
	}

	tbuf_append(reply, &result, sizeof(result));
	return reply;

      fail:
	result = 1;
	tbuf_append(reply, &result, sizeof(result));
	return reply;
}

bool
wal_write(struct recovery_state *r, i64 lsn, struct tbuf *data)
{
	struct tbuf *m = tbuf_alloc(data->pool);
	struct msg *a;

	say_debug("wal_write lsn=%" PRIi64, lsn);
	tbuf_reserve(m, sizeof(struct wal_write_request) + data->len);
	wal_write_request(m)->lsn = lsn;
	wal_write_request(m)->len = data->len;
	memcpy(wal_write_request(m)->data, data->data, data->len);

	if (write_inbox(r->wal_writer->out, m) == false) {
		say_warn("wal writer inbox is full");
		return false;
	}
	a = read_inbox();

	u32 reply = read_u32(a->msg);
	say_debug("wal_write reply=%" PRIu32, reply);
	if (reply != 0)
		say_warn("wal writer returned error status");
	return reply == 0;
}

struct recovery_state *
recover_init(const char *snap_dirname, const char *wal_dirname,
	     row_reader snap_row_reader, row_handler snap_row_handler, row_handler wal_row_handler,
	     int rows_per_file, double fsync_delay,
	     int inbox_size, int flags, void *data)
{
	struct recovery_state *r = p0alloc(eter_pool, sizeof(*r));

	r->wal_timer.data = r;
	r->snap_row_handler = snap_row_handler;
	r->wal_row_handler = wal_row_handler;
	r->data = data;

	r->snap_class = snap_classes(snap_row_reader, snap_dirname);
	r->snap_prefered_class = r->snap_class[1];

	r->wal_class = xlog_classes(wal_dirname);
	r->wal_prefered_class = r->wal_class[1];
	r->wal_prefered_class->rows_per_file = rows_per_file;
	r->wal_prefered_class->fsync_delay = fsync_delay;

	if ((flags & RECOVER_READONLY) == 0)
		r->wal_writer = spawn_child("wal_writer", inbox_size, write_to_disk, r);

	return r;
}

void
recovery_setup_panic(struct recovery_state *r, bool on_snap_error, bool on_wal_error)
{
	struct log_io_class **class;

	for (class = r->wal_class; *class; class++)
		(*class)->panic_if_error = on_wal_error;

	for (class = r->snap_class; *class; class++)
		(*class)->panic_if_error = on_snap_error;
}

static void
write_rows(struct log_io_iter *i)
{
	struct log_io *l = i->log;
	struct tbuf *row, *data;

	row = tbuf_alloc(eter_pool);
	tbuf_ensure(row, sizeof(struct row_v11));
	row->len = sizeof(struct row_v11);

	goto start;
	for (;;) {
		coro_transfer(&i->coro.ctx, &fiber->coro.ctx);
	      start:
		data = i->to;

		if (fwrite(&l->class->marker, l->class->marker_size, 1, l->f) != 1)
			panic("fwrite");

		row_v11(row)->lsn = 0;	/* unused */
		row_v11(row)->tm = ev_now();
		row_v11(row)->len = data->len;
		row_v11(row)->data_crc32c = crc32c(0, data->data, data->len);
		row_v11(row)->header_crc32c =
			crc32c(0, row->data + field_sizeof(struct row_v11, header_crc32c),
			       sizeof(struct row_v11) - field_sizeof(struct row_v11,
								     header_crc32c));

		if (fwrite(row->data, row->len, 1, l->f) != 1)
			panic("fwrite");

		if (fwrite(data->data, data->len, 1, l->f) != 1)
			panic("fwrite");
	}
}

void
snapshot_write_row(struct log_io_iter *i, struct tbuf *row)
{
	static int rows;
	static int bytes;
	static struct timeval last;

	i->to = row;
	if (i->io_rate_limit > 0) {
		if (last.tv_sec == 0)
			gettimeofday(&last, NULL);
		bytes += row->len;

		while (bytes >= i->io_rate_limit) {
			struct timeval now;
			useconds_t elapsed;

			gettimeofday(&now, NULL);
			elapsed = (now.tv_sec - last.tv_sec) * 1000000 + now.tv_usec - last.tv_usec;

			if (elapsed < 1000000)
				usleep(1000000 - elapsed);

			gettimeofday(&last, NULL);
			bytes -= i->io_rate_limit;
		}
	}
	coro_transfer(&fiber->coro.ctx, &i->coro.ctx);
	if (++rows % 100000 == 0)
		say_crit("%.1fM rows written", rows / 1000000.);
}

void
snapshot_save(struct recovery_state *r, void (*f) (struct log_io_iter *))
{
	struct log_io_iter i;
	struct log_io *snap;
	char final_filename[PATH_MAX + 1];
	char *dot;

	memset(&i, 0, sizeof(i));

	snap = open_for_write(r, r->snap_prefered_class, r->confirmed_lsn, -1);
	if (snap == NULL)
		panic("can't open snap for writing");

	iter_open(snap, &i, write_rows);

	if (r->snap_io_rate_limit > 0)
		i.io_rate_limit = r->snap_io_rate_limit;

	strncpy(final_filename, snap->filename, PATH_MAX);
	dot = strrchr(final_filename, '.');
	*dot = 0;

	say_info("saving snapshot `%s'", final_filename);
	f(&i);

	if (fsync(fileno(snap->f)) < 0)
		panic("fsync");

	if (rename(snap->filename, final_filename) != 0)
		panic("rename");

	close_log(&snap);

	say_info("done");
}
