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

#include <disk_fiber_io.h>
#include <coeio.h>
#include <fiber.h>
#include <say.h>
#include <stdio.h>
#include <stdlib.h>


struct fiber_eio {
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
			char *res;
		} tempdir;
	};
};

#define INIT_EIO(name)			\
	struct fiber_eio name;		\
	memset(&name, 0, sizeof(name));	\
	name.fiber = fiber();		\


static int
dfio_complete(eio_req *req)
{
	struct fiber_eio *eio = (struct fiber_eio *)req->data;

	eio->errorno = req->errorno;
	eio->done = true;
	eio->result = req->result;

	fiber_wakeup(eio->fiber);
	return 0;
}

static ssize_t
dfio_wait_done(eio_req *req, struct fiber_eio *eio)
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
dfio_open(const char *path, int flags, mode_t mode)
{
	INIT_EIO(eio);
	eio_req *req = eio_open(path, flags, mode, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

int
dfio_close(int fd)
{
	INIT_EIO(eio);
	eio_req *req = eio_close(fd, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

ssize_t
dfio_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
	INIT_EIO(eio);
	eio_req *req = eio_write(fd,
		(void *)buf, count, offset, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

ssize_t
dfio_pread(int fd, void *buf, size_t count, off_t offset)
{
	INIT_EIO(eio);
	eio_req *req = eio_read(fd, buf, count,
		offset, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

static void
dfio_do_write(eio_req *req)
{
	struct fiber_eio *eio = (struct fiber_eio *)req->data;
	req->result = write(eio->write.fd, eio->write.buf, eio->write.count);
	eio->errorno = errno;
}

ssize_t
dfio_write(int fd, const void *buf, size_t count)
{
	INIT_EIO(eio);
	eio.write.buf = buf;
	eio.write.count = count;
	eio.write.fd = fd;
	eio_req *req = eio_custom(dfio_do_write, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

static void
dfio_do_read(eio_req *req)
{
	struct fiber_eio *eio = (struct fiber_eio *)req->data;
	req->result = read(eio->read.fd, eio->read.buf, eio->read.count);
	req->errorno = errno;
}

ssize_t
dfio_read(int fd, void *buf, size_t count)
{
	INIT_EIO(eio);
	eio.read.buf = buf;
	eio.read.count = count;
	eio.read.fd = fd;
	eio_req *req = eio_custom(dfio_do_read, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}


static void
dfio_do_lseek(eio_req *req)
{
	struct fiber_eio *eio = (struct fiber_eio *)req->data;
	req->result =
		lseek(eio->lseek.fd, eio->lseek.offset, eio->lseek.whence);
	req->errorno = errno;
}

off_t
dfio_lseek(int fd, off_t offset, int whence)
{
	INIT_EIO(eio);

	eio.lseek.whence = whence;
	eio.lseek.offset = offset;
	eio.lseek.fd = fd;

	eio_req *req = eio_custom(dfio_do_lseek, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

static void
dfio_do_lstat(eio_req *req)
{
	struct fiber_eio *eio = (struct fiber_eio *)req->data;
	req->result = lstat(eio->lstat.pathname, eio->lstat.buf);
	req->errorno = errno;
}


int
dfio_lstat(const char *pathname, struct stat *buf)
{
	INIT_EIO(eio);
	eio.lstat.pathname = pathname;
	eio.lstat.buf = buf;
	eio_req *req = eio_custom(dfio_do_lstat, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

static void
dfio_do_stat(eio_req *req)
{
	struct fiber_eio *eio = (struct fiber_eio *)req->data;
	req->result = stat(eio->lstat.pathname, eio->lstat.buf);
	req->errorno = errno;
}

int
dfio_stat(const char *pathname, struct stat *buf)
{
	INIT_EIO(eio);
	eio.lstat.pathname = pathname;
	eio.lstat.buf = buf;
	eio_req *req = eio_custom(dfio_do_stat, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

static void
dfio_do_fstat(eio_req *req)
{
	struct fiber_eio *eio = (struct fiber_eio *)req->data;
	req->result = fstat(eio->fstat.fd, eio->fstat.buf);
	req->errorno = errno;
}

int
dfio_fstat(int fd, struct stat *stat)
{
	INIT_EIO(eio);
	eio.fstat.fd = fd;
	eio.fstat.buf = stat;

	eio_req *req = eio_custom(dfio_do_fstat, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

int
dfio_rename(const char *oldpath, const char *newpath)
{
	INIT_EIO(eio);
	eio_req *req = eio_rename(oldpath, newpath, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);

}

int
dfio_unlink(const char *pathname)
{
	INIT_EIO(eio);
	eio_req *req = eio_unlink(pathname, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

int
dfio_ftruncate(int fd, off_t length)
{
	INIT_EIO(eio);
	eio_req *req = eio_ftruncate(fd, length, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

int
dfio_truncate(const char *path, off_t length)
{
	INIT_EIO(eio);
	eio_req *req = eio_truncate(path, length, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

static void
dfio_do_glob(eio_req *req)
{
	struct fiber_eio *eio = (struct fiber_eio *)req->data;
	req->result = glob(eio->glob.pattern,
		eio->glob.flags, eio->glob.errfunc, eio->glob.pglob);
	req->errorno = errno;
}

int
dfio_glob(const char *pattern, int flags,
		int (*errfunc) (const char *epath, int eerrno),
		glob_t *pglob)
{
	INIT_EIO(eio);
	eio.glob.pattern = pattern;
	eio.glob.flags = flags;
	eio.glob.errfunc = errfunc;
	eio.glob.pglob = pglob;
	eio_req *req = eio_custom(dfio_do_glob, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

int
dfio_chown(const char *path, uid_t owner, gid_t group)
{
	INIT_EIO(eio);
	eio_req *req = eio_chown(path, owner, group, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

int
dfio_chmod(const char *path, mode_t mode)
{
	INIT_EIO(eio);
	eio_req *req = eio_chmod(path, mode, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

int
dfio_mkdir(const char *pathname, mode_t mode)
{
	INIT_EIO(eio);
	eio_req *req = eio_mkdir(pathname, mode, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

int
dfio_rmdir(const char *pathname)
{
	INIT_EIO(eio);
	eio_req *req = eio_rmdir(pathname, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

int
dfio_link(const char *oldpath, const char *newpath)
{
	INIT_EIO(eio);
	eio_req *req = eio_link(oldpath, newpath, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

int
dfio_symlink(const char *target, const char *linkpath)
{
	INIT_EIO(eio);
	eio_req *req = eio_symlink(target, linkpath, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

static void
dfio_do_readlink(eio_req *req)
{
	struct fiber_eio *eio = (struct fiber_eio *)req->data;
	req->result = readlink(eio->readlink.pathname,
		eio->readlink.buf, eio->readlink.bufsize);
	req->errorno = errno;
}

int
dfio_readlink(const char *pathname, char *buf, size_t bufsize)
{
	INIT_EIO(eio);
	eio.readlink.pathname = pathname;
	eio.readlink.buf = buf;
	eio.readlink.bufsize = bufsize;
	eio_req *req = eio_custom(dfio_do_readlink, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

static void
dfio_do_tempdir(eio_req *req)
{
	struct fiber_eio *eio = (struct fiber_eio *)req->data;
	char *res = mkdtemp(eio->tempdir.tpl);
	req->errorno = errno;
	if (res == NULL) {
		req->result = -1;
	} else {
		req->result = 0;
		eio->tempdir.res = res;
	}
}

const char *
dfio_tempdir()
{
	static __thread char path[PATH_MAX];
	INIT_EIO(eio);

	snprintf(path, PATH_MAX, "/tmp/XXXXXX");

	eio.tempdir.tpl = path;
	eio_req *req = eio_custom(dfio_do_tempdir, 0, dfio_complete, &eio);
	if (dfio_wait_done(req, &eio) == 0)
		return eio.tempdir.res;
	return NULL;
}

int
dfio_sync()
{
	INIT_EIO(eio);
	eio_req *req = eio_sync(0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

int
dfio_fsync(int fd)
{
	INIT_EIO(eio);
	eio_req *req = eio_fsync(fd, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}

int
dfio_fdatasync(int fd)
{
	INIT_EIO(eio);
	eio_req *req = eio_fdatasync(fd, 0, dfio_complete, &eio);
	return dfio_wait_done(req, &eio);
}
