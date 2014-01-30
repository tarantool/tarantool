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
#include <malloc.h>
#include <stdlib.h>
#include <errno.h>
#include <say.h>
#include <assert.h>
#include <unistd.h>
#include <tarantool/config.h>


struct fiob {
	int fd;
	size_t bsize;
	size_t bfill;
	void *buf;
	char *path;
	#ifdef HAVE_FUNOPEN
		struct {
			int     (*read)(void *cookie, char *buf, int len);
			int     (*write)(void *cookie, char *buf, int len);
			fpos_t  (*seek)(void *cookie, fpos_t pos, int whence);
			int     (*close)(void *cookie);
		} io;
	#else
		cookie_io_functions_t io;
	#endif
};

static ssize_t
fiob_readf(struct fiob *f, char *buf, size_t count)
{
	ssize_t to_read = (ssize_t) count;
	while (to_read > 0) {
		ssize_t nrd = read(f->fd, buf, to_read);
		if (nrd < 0) {
			if (errno == EINTR) {
				errno = 0;
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return count != to_read ? count - to_read : -1;
			say_syserror("read, [%s]", f->path);
			return -1; /* XXX: file position is unspecified */
		}
		if (nrd == 0)
			break;

		buf += nrd;
		to_read -= nrd;
	}
	return count - to_read;
}

#ifdef HAVE_FUNOPEN
static int
fiob_read(void *cookie, char *buf, int len)
#else
static ssize_t
fiob_read(void *cookie, char *buf, size_t len)
#endif
{
	struct fiob *f = (struct fiob *)cookie;
	return fiob_readf(f, buf, len);
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
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                                return count != to_write ? count - to_write : -1;
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
	if (!f->buf || !f->bfill)
		return 0;

	off_t cur = lseek(f->fd, 0L, SEEK_CUR);
	if (cur == (off_t)-1)
		return -1;

	off_t size = lseek(f->fd, 0L, SEEK_END);
	if (size == (off_t)-1)
		return -1;

	if (lseek(f->fd, cur, SEEK_SET) == (off_t)-1)
		return -1;

	assert(cur + f->bfill >= size);

	if (fiob_writef(f, f->buf, f->bsize) < 0)
		return -1;

	if (lseek(f->fd, cur + f->bfill, SEEK_SET) == (off_t)-1)
		return -1;

	int res = ftruncate(f->fd, cur + f->bfill);
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
	ssize_t wrdone;

	if (f->buf) {
		/* append buffer */
		if (f->bsize - f->bfill >= len) {
			memcpy(f->buf + f->bfill, buf, len);
			f->bfill += len;
			return len;
		}


		/* buffer is full */
		if (f->bfill >= f->bsize) {
			wrdone = fiob_writef(f, f->buf, f->bsize);
			if (wrdone < 0)
				return wrdone;

			f->bfill = 0;
			return fiob_write(cookie, buf, len);
		}

		/* data is longer than buffer */
		memcpy(f->buf + f->bfill, buf, f->bsize - f->bfill);

		wrdone = fiob_writef(f, f->buf, f->bsize);



		if (wrdone < 0)
			return wrdone;

		wrdone -= f->bfill;

		f->bfill = 0;
		buf += wrdone;
		len -= wrdone;


		if (len > 0) {
			ssize_t wrtail = fiob_write(cookie, buf, len);
			if (wrtail < 0)
				return wrtail;
			wrdone += wrtail;
		}
		return wrdone;
	}

	return fiob_writef(f, buf, len);
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
	int res = 0;
	if (fiob_flushb(f) < 0)
		res = -1;

	int save_errno = errno;

	if (close(f->fd) < 0) {
		if (res < 0)
			errno = save_errno;
		res = -1;
	}


	free(f->buf);
	free(f->path);
	free(f);

	errno = save_errno;
	return res;
}

/** open file. The same as fiob_open but receives additional open (2) flags */
FILE *
fiob_open(const char *path, const char *mode)
{
	int omode = 0666;
	int flags = 0;

	size_t bsize = 0;
	void *buf = NULL;

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
	if (strchr(mode, 'x')) {
		flags |= O_EXCL;
	}
	#endif

	/* O_DIRECT */
	if (strchr(mode, 'd')) {
		#ifdef O_DIRECT
			flags |= O_DIRECT;
		#endif
		bsize = O_DIRECT_BSIZE;
		posix_memalign(&buf, 4096, bsize);
		if (!buf) {
			errno = ENOMEM;
			return NULL;
		}
		/* for valgrind */
		memset(buf, 0, bsize);
	}

	/* O_SYNC */
	if (strchr(mode, 's')) {
		flags |= WAL_SYNC_FLAG;
	}

	struct fiob *f = (struct fiob *)calloc(1, sizeof(struct fiob));
	if (!f) {
		free(buf);
		errno = ENOMEM;
		return NULL;
	}

	f->path = strdup(path);
	if (!f->path) {
		errno = ENOMEM;
		goto error;
	}

	f->buf = buf;
	f->bsize = bsize;

	f->fd = open(path, flags, omode);
	if (f->fd < 0)
		goto error;



	f->io.read	= fiob_read;
	f->io.write	= fiob_write;
	f->io.seek	= fiob_seek;
	f->io.close	= fiob_close;

	FILE *file;
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

error: {
		int save_errno = errno;
		say_syserror("Can't open '%s'", path);
		if (f->fd > 0)
			close(f->fd);

		free(f->buf);
		free(f->path);
		free(f);

		errno = save_errno;
	}
	return NULL;
}

