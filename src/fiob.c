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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <fiob.h>

#include <sys/types.h>
#include <stdbool.h>
#include <sys/uio.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <say.h>
#include <assert.h>
#include <unistd.h>
#include <trivia/config.h>
#include <trivia/util.h>

/* Use special implemention if we have O_DIRECT and FOPENCOOKIE or FUNOPEN */
#if defined(O_DIRECT) && (defined(HAVE_FUNOPEN) || defined(HAVE_FOPENCOOKIE))
#define FIOB_DIRECT
#endif

#if defined(FIOB_DIRECT)
enum {
	FIOB_ALIGN = 4096,
	FIOB_BSIZE = FIOB_ALIGN * 256
};

struct fiob {
	int fd;
	size_t bsize;
	size_t bfill;
	void *buf;
	char *path;
#ifdef HAVE_FUNOPEN
	struct {
		int     (*read)(void *cookie, char *buf, int len);
		int     (*write)(void *cookie, const char *buf, int len);
		fpos_t  (*seek)(void *cookie, fpos_t pos, int whence);
		int     (*close)(void *cookie);
	} io;
#else
	cookie_io_functions_t io;
#endif
};

static inline off_t
fiob_ceil(off_t off)
{
	/* ceil to FIOB_ALIGN */
	return (off + FIOB_ALIGN - 1) & ~(off_t) (FIOB_ALIGN - 1);
}

#ifdef HAVE_FUNOPEN
static int
fiob_read(void *cookie, char *buf, int count)
#else
static ssize_t
fiob_read(void *cookie, char *buf, size_t count)
#endif
{
	struct fiob *f = (struct fiob *)cookie;
	ssize_t to_read = (ssize_t) count;

	/* The number of starting bytes in f->buf to skip due to alignment */
	off_t skip = 0;
	while (to_read > 0) {
		/* Align `to_read' FIOB_ALIGN to be <= size of f->buf */
		ssize_t to_read_al = MIN(fiob_ceil(to_read), f->bsize);
		/*
		 * Optimistically try to read aligned size into the aligned
		 * buffer. If the current file position is not aligned then
		 * read(2) returns EINVAL. In this case seek to an aligned
		 * position and try again. This trick saves one extra
		 * syscall for general workflow.
		 */
		ssize_t nrd = read(f->fd, f->buf, to_read_al);
		if (nrd < 0) {
			if (errno == EINTR) {
				errno = 0;
				continue;
			} else if (errno == EINVAL && skip == 0) {
				/*
				 * read(2) can return EINVAL only in 3 cases:
				 *  1. read buffer is not aligned - handled in
				 *     fiob_open().
				 *  2. read size is not aligned - handled above
				 *  3. current file position is not aligned -
				 *     handled here.
				 */
				off_t pos = lseek(f->fd, 0, SEEK_CUR);
				if (pos < 0) {
					say_syserror("lseek, [%s]", f->path);
					return -1;
				}
				/* Calculate aligned position */
				skip = pos % FIOB_ALIGN;
				pos -= skip;
				if (skip == 0) {
					/* Position is aligned. */
					errno = EINVAL;
					say_error("read, [%s]", f->path);
					return -1;
				}
				/* Seek to the new position */
				if (lseek(f->fd, pos, SEEK_SET) != pos) {
					say_syserror("lseek, [%s]", f->path);
					return -1;
				}
				/* Try to read again. */
				continue;
			}
			say_syserror("read, [%s]", f->path);
			return -1; /* XXX: file position is unspecified */
		}
		/* Ignore starting bytes if the position was aligned. */
		nrd -= skip;
		if (nrd == 0)
			break;
		if (nrd > to_read) {
			/*
			 * A few more bytes have been read because `to_read'
			 * is not aligned to FIOB_ALIGN. Set the file position
			 * to the expected libc value and ignore extra bytes.
			 */
			if (lseek(f->fd, to_read - nrd, SEEK_CUR) < 0) {
				say_syserror("lseek, [%s]", f->path);
				return -1;
			}
			nrd = to_read;
		}

		memcpy(buf, f->buf + skip, nrd); /* see nrd -= skip */
		skip = 0; /* reset alignment offset */
		buf += nrd;
		to_read -= nrd;
	}
	return count - to_read;
}

static ssize_t
fiob_writef(struct fiob *f, const char *buf, size_t count)
{
	int fd = f->fd;
	ssize_t to_write = (ssize_t) count;
	while (to_write > 0) {
		ssize_t nwr = write(fd, buf, to_write);
		if (nwr < 0) {
			if (errno == EINTR) {
				errno = 0;
				continue;
			}
			say_syserror("write, [%s]", f->path);
			return -1; /* XXX: file position is unspecified */
		}
		if (nwr == 0)
			break;

		buf += nwr;
		to_write -= nwr;
	}
	return count - to_write;
}

static int
fiob_flushb(struct fiob *f)
{
	if (!f->bfill)
		return 0;

	size_t tlen = fiob_ceil(f->bfill);

	if (fiob_writef(f, f->buf, tlen) < 0) {
		return -1;
	}

	off_t size = lseek(f->fd, (off_t)(f->bfill) - tlen, SEEK_CUR);
	if (size == (off_t)-1) {
		return -1;
	}
	int res = ftruncate(f->fd, size);

	f->bfill = 0;
	return res;
}


#ifdef HAVE_FUNOPEN
int
fiob_write(void *cookie, const char *buf, int len)
#else
ssize_t
fiob_write(void *cookie, const char *buf, size_t len)
#endif
{
	struct fiob *f = (struct fiob *)cookie;

	if (len == 0)
		return 0;

	ssize_t bytes_left = len;
	ssize_t tocopy;

	if (f->bfill < f->bsize) {
		ssize_t available_buf_size = f->bsize - f->bfill;
		tocopy = available_buf_size > bytes_left ?
			bytes_left : available_buf_size;

		memcpy(f->buf + f->bfill, buf, tocopy);
		bytes_left -= tocopy;
		buf += tocopy;
		f->bfill += tocopy;
	}
	while (bytes_left > 0) {
		assert(f->bfill == f->bsize);
		ssize_t res = fiob_writef(f, f->buf, f->bsize);
		if (res < 0)
#if defined(HAVE_FUNOPEN)
			return res;
#else
			return 0;
#endif
		tocopy = f->bsize > bytes_left ? bytes_left : f->bsize;
		/*
		 * We must memcpy because O_DIRECT requires
		 * an aligned chunk.
		 */
		memcpy(f->buf, buf, tocopy);
		bytes_left -= tocopy;
		buf += tocopy;
		f->bfill = tocopy;
	}
	return len;
}

#ifdef HAVE_FUNOPEN
static fpos_t
fiob_seek(void *cookie, fpos_t pos, int whence)
{
	struct fiob *f = (struct fiob *)cookie;
	if (fiob_flushb(f) < 0)
		return -1;

	return lseek(f->fd, pos, whence);
}
#else
static int
fiob_seek(void *cookie, off64_t *pos, int whence)
{
	struct fiob *f = (struct fiob *)cookie;
	if (fiob_flushb(f) < 0)
		return -1;

	off_t newpos = lseek(f->fd, *pos, whence);

	if (newpos == (off_t)-1)
		return -1;

	*pos = newpos;
	return 0;
}
#endif

static int
fiob_close(void *cookie)
{
	struct fiob *f = (struct fiob *)cookie;

	int res = fiob_flushb(f);
	int save_errno = errno;

	if (close(f->fd) < 0 && res == 0) {
		res = -1;
		save_errno = errno;
	}

	free(f->buf);
	free(f->path);
	free(f);

	errno = save_errno;
	return res;
}
#endif /* defined(FIOB_DIRECT) */

/** open file. The same as fopen but receives additional open (2) flags */
FILE *
fiob_open(const char *path, const char *mode)
{
	int omode = 0666;
	int flags = 0;
	int save_errno;
	int fd = -1;
	FILE *file = NULL;
#if defined (FIOB_DIRECT)
	struct fiob *f = NULL;
#endif /* defined(FIOB_DIRECT) */
	int um = umask(0722);
	umask(um);
	omode &= ~um;

	if (strchr(mode, 'r')) {
		if (strchr(mode, '+'))
			flags |= O_RDWR;
		else
			flags |= O_RDONLY;
	} else if (strchr(mode, 'w')) {
		flags |= O_TRUNC | O_CREAT;
		if (strchr(mode, '+'))
			flags |= O_RDWR;
		else
			flags |= O_WRONLY;
	} else if (strchr(mode, 'a')) {
		flags |= O_CREAT | O_APPEND;
		if (strchr(mode, '+'))
			flags |= O_RDWR;
		else
			flags |= O_WRONLY;
	} else {
		errno = EINVAL;
		return NULL;
	}

	/* O_EXCL */
#ifdef O_EXCL
	if (strchr(mode, 'x'))
		flags |= O_EXCL;
#endif
	/* O_SYNC */
	if (strchr(mode, 's')) {
		flags |= WAL_SYNC_FLAG;
	}

	fd = open(path, flags, omode);
	if (fd < 0)
		goto error;
#if defined(FIOB_DIRECT)
	if (strchr(mode, 'd') == NULL)
		goto fdopen;

	/* Try to enable O_DIRECT */
	flags = fcntl(fd, F_GETFL);
	if (flags != -1 && fcntl(fd, F_SETFL, flags | O_DIRECT) != -1) {
		say_debug("using O_DIRECT for %s", path);
	} else {
#if defined(NDEBUG) /* Don't use opencookie in release mode without O_DIRECT */
		goto fdopen;
#endif /* defined(NDEBUG) */
	}

	f = (struct fiob *)calloc(1, sizeof(struct fiob));
	if (!f)
		goto error;

	f->fd = fd;
	f->bsize = FIOB_BSIZE;
	if (posix_memalign(&f->buf, FIOB_ALIGN, f->bsize))
		goto error;

	/* for valgrind */
	memset(f->buf, 0, f->bsize);

	f->path = strdup(path);
	if (!f->path)
		goto error;

	f->io.read	= fiob_read;
	f->io.write	= fiob_write;
	f->io.seek	= fiob_seek;
	f->io.close	= fiob_close;

#ifdef HAVE_FUNOPEN
	file = funopen(f,
		       f->io.read, f->io.write, f->io.seek, f->io.close);
#else
	file = fopencookie(f, mode, f->io);
#endif

	if (!file)
		goto error;

#ifdef TARGET_OS_LINUX
	file->_fileno = f->fd;
#else
	file->_file = f->fd;
#endif

	return file;

fdopen:
#endif /* defined(FIOB_DIRECT) */
	/* Fallback to libc implementation */
	file = fdopen(fd, mode);
	if (!file)
		goto error;
	return file;

error:
	save_errno = errno;
	say_syserror("Can't open '%s'", path);
	if (fd >= 0)
		close(fd);

#if defined(FIOB_DIRECT)
	if (f) {
		free(f->buf);
		free(f->path);
		free(f);
	}
#endif /* FIOB_DIRECT */

	errno = save_errno;
	return NULL;
}

