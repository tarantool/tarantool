#ifndef INCLUDES_TARANTOOL_COEIO_FILE_H
#define INCLUDES_TARANTOOL_COEIO_FILE_H
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

int     coeio_file_open(const char *path, int flags, mode_t mode);
int     coeio_file_close(int fd);

ssize_t coeio_file_pwrite(int fd, const void *buf, size_t count, off_t offset);
ssize_t coeio_file_pread(int fd, void *buf, size_t count, off_t offset);
ssize_t coeio_file_read(int fd, void *buf, size_t count);
ssize_t coeio_file_write(int fd, const void *buf, size_t count);
off_t   coeio_file_lseek(int fd, off_t offset, int whence);

int     coeio_file_stat(const char *pathname, struct stat *buf);
int     coeio_file_lstat(const char *pathname, struct stat *buf);
int     coeio_file_fstat(int fd, struct stat *buf);
int     coeio_file_rename(const char *oldpath, const char *newpath);
int     coeio_file_unlink(const char *pathname);
int     coeio_file_mkdir(const char *pathname, mode_t mode);
int     coeio_file_rmdir(const char *pathname);
int     coeio_file_ftruncate(int fd, off_t length);
int     coeio_file_truncate(const char *path, off_t length);
int     coeio_file_glob(const char *pattern, int flags,
		int (*errfunc) (const char *epath, int eerrno),
		glob_t *pglob);
int     coeio_file_chown(const char *path, uid_t owner, gid_t group);
int     coeio_file_chmod(const char *path, mode_t mode);

int     coeio_file_link(const char *oldpath, const char *newpath);
int     coeio_file_symlink(const char *target, const char *linkpath);
int     coeio_file_readlink(const char *pathname, char *buf, size_t bufsiz);

int     coeio_file_sync();
int     coeio_file_fsync(int fd);
int     coeio_file_fdatasync(int fd);


int	coeio_file_tempdir(char *path, size_t path_len);

#endif /* INCLUDES_TARANTOOL_COEIO_FILE_H */
