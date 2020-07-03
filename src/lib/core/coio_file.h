#ifndef TARANTOOL_LIB_CORE_COIO_FILE_H_INCLUDED
#define TARANTOOL_LIB_CORE_COIO_FILE_H_INCLUDED
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
#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <sys/types.h>
#include <sys/stat.h>
#include <glob.h>

/**
 * Cooperative file I/O.
 * Unlike the rest of coio API, this implementation
 * doesn't support timeouts or cancellation.
 *
 * It follows the error reporting convention of the respective
 * system calls, i.e. it doesn't throw exceptions either.
 */

int     coio_file_open(const char *path, int flags, mode_t mode);
int     coio_file_close(int fd);

ssize_t coio_pwrite(int fd, const void *buf, size_t count, off_t offset);
ssize_t coio_pread(int fd, void *buf, size_t count, off_t offset);
ssize_t coio_preadn(int fd, void *buf, size_t count, off_t offset);
ssize_t coio_read(int fd, void *buf, size_t count);
ssize_t coio_write(int fd, const void *buf, size_t count);
off_t   coio_lseek(int fd, off_t offset, int whence);

int     coio_stat(const char *pathname, struct stat *buf);
int     coio_lstat(const char *pathname, struct stat *buf);
int     coio_fstat(int fd, struct stat *buf);
int     coio_rename(const char *oldpath, const char *newpath);
int     coio_unlink(const char *pathname);
int     coio_mkdir(const char *pathname, mode_t mode);
int     coio_rmdir(const char *pathname);
int     coio_ftruncate(int fd, off_t length);
int     coio_truncate(const char *path, off_t length);
int     coio_glob(const char *pattern, int flags,
		   int (*errfunc) (const char *epath, int eerrno),
		   glob_t *pglob);
int     coio_chown(const char *path, uid_t owner, gid_t group);
int     coio_chmod(const char *path, mode_t mode);

int     coio_link(const char *oldpath, const char *newpath);
int     coio_symlink(const char *target, const char *linkpath);
int     coio_readlink(const char *pathname, char *buf, size_t bufsiz);

int     coio_sync(void);
int     coio_fsync(int fd);
int     coio_fdatasync(int fd);

int	coio_tempdir(char *path, size_t path_len);

int	coio_readdir(const char *path, char **buf);
int	coio_copyfile(const char *source, const char *dest);
int	coio_utime(const char *pathname, double atime, double mtime);
#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_COIO_FILE_H_INCLUDED */
