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
#include "coio_file.h"
#include "coio_task.h"
#include "fiber.h"
#include "say.h"
#include "fio.h"
#include "errinj.h"
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

/**
 * A context of libeio request for any
 * coio task.
 */
struct coio_file_task {
	ssize_t result;
	int errorno;
	struct fiber *fiber;
	bool done;

	union {
		struct {
			int fd;
			struct stat *buf;
		} fstat;

		struct {
			struct stat *buf;
			const char *pathname;
		} lstat;

		struct {
			const char *pattern;
			int flags;
			int (*errfunc) (const char *epath, int eerrno);
			glob_t *pglob;
		} glob;

		struct {
			int fd;
			off_t offset;
			int whence;
		} lseek;

		struct {
			int fd;
			const void *buf;
			size_t count;
		} write;

		struct {
			int fd;
			void *buf;
			size_t count;
		} read;

		struct {
			const char *pathname;
			char *buf;
			size_t bufsize;
		} readlink;

		struct {
			char *tpl;
		} tempdir;

		struct {
			char **bufp;
			const char *pathname;
		} readdir;

		struct {
			const char *source;
			const char *dest;
		} copyfile;
	};
};

#define INIT_COEIO_FILE(name)			\
	struct coio_file_task name;		\
	memset(&name, 0, sizeof(name));		\
	name.fiber = fiber();			\

/** A callback invoked by eio when a task is complete. */
static int
coio_complete(eio_req *req)
{
	struct coio_file_task *eio = (struct coio_file_task *)req->data;

	eio->errorno = req->errorno;
	eio->done = true;
	eio->result = req->result;

	fiber_wakeup(eio->fiber);
	return 0;
}

/**
 * Synchronously (from cooperative multitasking point of view)
 * wait for task completion.
 */
static ssize_t
coio_wait_done(eio_req *req, struct coio_file_task *eio)
{
	if (!req) {
		errno = ENOMEM;
		return -1;
	}

	while (!eio->done)
		fiber_yield();

	errno = eio->errorno;
	return eio->result;
}

int
coio_file_open(const char *path, int flags, mode_t mode)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_open(path, flags, mode, 0,
				coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_file_close(int fd)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_close(fd, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

ssize_t
coio_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
	ssize_t left = count, pos = 0, res, chunk;
	eio_req *req;

	while (left > 0) {
		INIT_COEIO_FILE(eio);
		chunk = left;

		ERROR_INJECT(ERRINJ_COIO_WRITE_CHUNK, {
			chunk = 1;
		});

		req = eio_write(fd, (char *)buf + pos, chunk,
				offset + pos, EIO_PRI_DEFAULT,
				coio_complete, &eio);
		res = coio_wait_done(req, &eio);
		if (res < 0) {
			pos = -1;
			break;
		} else {
			left -= res;
			pos += res;
		}
	}
	return pos;
}

ssize_t
coio_pread(int fd, void *buf, size_t count, off_t offset)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_read(fd, buf, count,
				offset, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

ssize_t
coio_preadn(int fd, void *buf, size_t count, off_t offset)
{
	size_t n = 0;
	do {
		ssize_t r;
		do {
			r = coio_pread(fd, buf + n, count - n, offset + n);
		} while (r == -1 && errno == EINTR);
		if (r <= 0)
			return -1;
		n += r;
	} while (n < count);

	assert(n == count);
	return n;
}

static void
coio_do_write(eio_req *req)
{
	struct coio_file_task *eio = (struct coio_file_task *)req->data;

	ERROR_INJECT(ERRINJ_COIO_WRITE_CHUNK, {
		eio->write.count = 1;
	});

	req->result = write(eio->write.fd, eio->write.buf, eio->write.count);
	eio->errorno = errno;
}

ssize_t
coio_write(int fd, const void *buf, size_t count)
{
	ssize_t left = count, pos = 0, res;
	eio_req *req;

	while (left > 0) {
		INIT_COEIO_FILE(eio);

		eio.write.buf	= (char *)buf + pos;
		eio.write.count	= left;
		eio.write.fd	= fd;

		req = eio_custom(coio_do_write, EIO_PRI_DEFAULT,
				 coio_complete, &eio);
		res = coio_wait_done(req, &eio);
		if (res < 0) {
			pos = -1;
			break;
		} else {
			left -= res;
			pos += res;
		}
	}
	return pos;
}

static void
coio_do_read(eio_req *req)
{
	struct coio_file_task *eio = (struct coio_file_task *)req->data;
	req->result = read(eio->read.fd, eio->read.buf, eio->read.count);
	req->errorno = errno;
}

ssize_t
coio_read(int fd, void *buf, size_t count)
{
	INIT_COEIO_FILE(eio);
	eio.read.buf = buf;
	eio.read.count = count;
	eio.read.fd = fd;
	eio_req *req = eio_custom(coio_do_read, 0,
				  coio_complete, &eio);
	return coio_wait_done(req, &eio);
}


static void
coio_do_lseek(eio_req *req)
{
	struct coio_file_task *eio = (struct coio_file_task *)req->data;
	req->result =
		lseek(eio->lseek.fd, eio->lseek.offset, eio->lseek.whence);
	req->errorno = errno;
}

off_t
coio_lseek(int fd, off_t offset, int whence)
{
	INIT_COEIO_FILE(eio);

	eio.lseek.whence = whence;
	eio.lseek.offset = offset;
	eio.lseek.fd = fd;

	eio_req *req = eio_custom(coio_do_lseek, 0,
				  coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

static void
coio_do_lstat(eio_req *req)
{
	struct coio_file_task *eio = (struct coio_file_task *)req->data;
	req->result = lstat(eio->lstat.pathname, eio->lstat.buf);
	req->errorno = errno;
}

int
coio_lstat(const char *pathname, struct stat *buf)
{
	INIT_COEIO_FILE(eio);
	eio.lstat.pathname = pathname;
	eio.lstat.buf = buf;
	eio_req *req = eio_custom(coio_do_lstat, 0,
				  coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

static void
coio_do_stat(eio_req *req)
{
	struct coio_file_task *eio = (struct coio_file_task *)req->data;
	req->result = stat(eio->lstat.pathname, eio->lstat.buf);
	req->errorno = errno;
}

int
coio_stat(const char *pathname, struct stat *buf)
{
	INIT_COEIO_FILE(eio);
	eio.lstat.pathname = pathname;
	eio.lstat.buf = buf;
	eio_req *req = eio_custom(coio_do_stat, 0,
				  coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

static void
coio_do_fstat(eio_req *req)
{
	struct coio_file_task *eio = (struct coio_file_task *)req->data;
	req->result = fstat(eio->fstat.fd, eio->fstat.buf);
	req->errorno = errno;
}

int
coio_fstat(int fd, struct stat *stat)
{
	INIT_COEIO_FILE(eio);
	eio.fstat.fd = fd;
	eio.fstat.buf = stat;

	eio_req *req = eio_custom(coio_do_fstat, 0,
				  coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_rename(const char *oldpath, const char *newpath)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_rename(oldpath, newpath, 0,
				  coio_complete, &eio);
	return coio_wait_done(req, &eio);

}

int
coio_unlink(const char *pathname)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_unlink(pathname, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_ftruncate(int fd, off_t length)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_ftruncate(fd, length, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_truncate(const char *path, off_t length)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_truncate(path, length, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

static void
coio_do_glob(eio_req *req)
{
	struct coio_file_task *eio = (struct coio_file_task *)req->data;
	req->result = glob(eio->glob.pattern,
			   eio->glob.flags, eio->glob.errfunc, eio->glob.pglob);
	req->errorno = errno;
}

int
coio_glob(const char *pattern, int flags,
		int (*errfunc) (const char *epath, int eerrno),
		glob_t *pglob)
{
	INIT_COEIO_FILE(eio);
	eio.glob.pattern = pattern;
	eio.glob.flags = flags;
	eio.glob.errfunc = errfunc;
	eio.glob.pglob = pglob;
	eio_req *req =
		eio_custom(coio_do_glob, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_chown(const char *path, uid_t owner, gid_t group)
{
	INIT_COEIO_FILE(eio);
	eio_req *req =
		eio_chown(path, owner, group, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_chmod(const char *path, mode_t mode)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_chmod(path, mode, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_mkdir(const char *pathname, mode_t mode)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_mkdir(pathname, mode, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_rmdir(const char *pathname)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_rmdir(pathname, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_link(const char *oldpath, const char *newpath)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_link(oldpath, newpath, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_symlink(const char *target, const char *linkpath)
{
	INIT_COEIO_FILE(eio);
	eio_req *req =
		eio_symlink(target, linkpath, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

static void
coio_do_readlink(eio_req *req)
{
	struct coio_file_task *eio = (struct coio_file_task *)req->data;
	req->result = readlink(eio->readlink.pathname,
			       eio->readlink.buf, eio->readlink.bufsize);
	req->errorno = errno;
}

int
coio_readlink(const char *pathname, char *buf, size_t bufsize)
{
	INIT_COEIO_FILE(eio);
	eio.readlink.pathname = pathname;
	eio.readlink.buf = buf;
	eio.readlink.bufsize = bufsize;
	eio_req *req = eio_custom(coio_do_readlink, 0,
				  coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

static void
coio_do_tempdir(eio_req *req)
{
	struct coio_file_task *eio = (struct coio_file_task *)req->data;
	char *res = mkdtemp(eio->tempdir.tpl);
	req->errorno = errno;
	if (res == NULL) {
		req->result = -1;
	} else {
		req->result = 0;
	}
}

int
coio_tempdir(char *path, size_t path_len)
{
	INIT_COEIO_FILE(eio);

	const char *tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL)
		tmpdir = "/tmp";
	int rc = snprintf(path, path_len, "%s/XXXXXX", tmpdir);
	if (rc < 0)
		return -1;
	if ((size_t) rc >= path_len) {
		errno = ENOMEM;
		return -1;
	}
	eio.tempdir.tpl = path;
	eio_req *req =
		eio_custom(coio_do_tempdir, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_sync(void)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_sync(0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_fsync(int fd)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_fsync(fd, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_fdatasync(int fd)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_fdatasync(fd, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

static void
coio_do_readdir(eio_req *req)
{
	struct coio_file_task *eio = (struct coio_file_task *)req->data;
	DIR *dirp = opendir(eio->readdir.pathname);
	if (dirp == NULL)
		goto error;
	size_t capacity = 128;
	size_t len = 0;
	struct dirent *entry;
	char *buf = (char *) malloc(capacity);
	if (buf == NULL)
		goto mem_error;
	req->result = 0;
	do {
		entry = readdir(dirp);
		if (entry == NULL ||
		    strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;
		size_t namlen = strlen(entry->d_name);
		size_t needed = len + namlen + 1;
		if (needed > capacity) {
			if (needed <= capacity * 2)
				capacity *= 2;
			else
				capacity = needed * 2;
			char *new_buf = (char *) realloc(buf, capacity);
			if (new_buf == NULL)
				goto mem_error;
			buf = new_buf;
		}
		memcpy(&buf[len], entry->d_name, namlen);
		len += namlen;
		buf[len++] = '\n';
		req->result++;
	} while(entry != NULL);

	if (len > 0)
		buf[len - 1] = 0;
	else
		buf[0] = 0;

	*eio->readdir.bufp = buf;
	closedir(dirp);
	return;

mem_error:
	free(buf);
	closedir(dirp);
error:
	req->result = -1;
	req->errorno = errno;
}

int
coio_readdir(const char *dir_path, char **buf)
{
	INIT_COEIO_FILE(eio)
	eio.readdir.bufp = buf;
	eio.readdir.pathname = dir_path;
	eio_req *req = eio_custom(coio_do_readdir, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

static void
coio_do_copyfile(eio_req *req)
{
	struct errinj *inj = errinj(ERRINJ_COIO_SENDFILE_CHUNK, ERRINJ_INT);
	struct coio_file_task *eio = (struct coio_file_task *)req->data;
	off_t pos, ret, left, chunk;
	struct stat st;
	if (stat(eio->copyfile.source, &st) < 0) {
		goto error;
	}

	int source_fd = open(eio->copyfile.source, O_RDONLY);
	if (source_fd < 0) {
		goto error;
	}

	int dest_fd = open(eio->copyfile.dest, O_WRONLY|O_CREAT|O_TRUNC,
			   st.st_mode & 0777);
	if (dest_fd < 0) {
		goto error_dest;
	}

	if (inj != NULL && inj->iparam > 0)
		chunk = (off_t)inj->iparam;
	else
		chunk = st.st_size;

	for (left = st.st_size, pos = 0; left > 0;) {
		ret = eio_sendfile_sync(dest_fd, source_fd, pos, chunk);
		if (ret < 0) {
			say_syserror("sendfile, [%s -> %s]",
				     fio_filename(source_fd),
				     fio_filename(dest_fd));
			goto error_copy;
		}
		pos += ret;
		left -= ret;
	}

	req->result = 0;
	close(source_fd);
	close(dest_fd);
	return;

error_copy:
	close(dest_fd);
error_dest:
	close(source_fd);
error:
	req->errorno = errno;
	req->result = -1;
	return;
}

int
coio_copyfile(const char *source, const char *dest)
{
	INIT_COEIO_FILE(eio)
	eio.copyfile.source = source;
	eio.copyfile.dest = dest;
	eio_req *req = eio_custom(coio_do_copyfile, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}

int
coio_utime(const char *pathname, double atime, double mtime)
{
	INIT_COEIO_FILE(eio);
	eio_req *req = eio_utime(pathname, atime, mtime, 0, coio_complete, &eio);
	return coio_wait_done(req, &eio);
}
