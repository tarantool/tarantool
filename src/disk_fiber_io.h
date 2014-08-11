#ifndef INCLUDES_TARANTOOL_LUA_DISK_FIBER_IO_H
#define INCLUDES_TARANTOOL_LUA_DISK_FIBER_IO_H
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

#include <sys/types.h>
#include <glob.h>

int     dfio_open(const char *path, int flags, mode_t mode);
int     dfio_close(int fd);

ssize_t dfio_pwrite(int fd, const void *buf, size_t count, off_t offset);
ssize_t dfio_pread(int fd, void *buf, size_t count, off_t offset);
ssize_t dfio_read(int fd, void *buf, size_t count);
ssize_t dfio_write(int fd, const void *buf, size_t count);
off_t   dfio_lseek(int fd, off_t offset, int whence);

int     dfio_stat(const char *pathname, struct stat *buf);
int     dfio_lstat(const char *pathname, struct stat *buf);
int     dfio_fstat(int fd, struct stat *buf);
int     dfio_rename(const char *oldpath, const char *newpath);
int     dfio_unlink(const char *pathname);
int     dfio_mkdir(const char *pathname, mode_t mode);
int     dfio_rmdir(const char *pathname);
int     dfio_ftruncate(int fd, off_t length);
int     dfio_truncate(const char *path, off_t length);
int     dfio_glob(const char *pattern, int flags,
		int (*errfunc) (const char *epath, int eerrno),
		glob_t *pglob);
int     dfio_chown(const char *path, uid_t owner, gid_t group);
int     dfio_chmod(const char *path, mode_t mode);

int     dfio_link(const char *oldpath, const char *newpath);
int     dfio_symlink(const char *target, const char *linkpath);
int     dfio_readlink(const char *pathname, char *buf, size_t bufsiz);

int     dfio_sync();
int     dfio_fsync(int fd);
int     dfio_fdatasync(int fd);


const char *dfio_tempdir();

#endif /* INCLUDES_TARANTOOL_LUA_DISK_FIBER_IO_H */
