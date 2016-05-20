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
#include "fio.h"

#include <sys/types.h>

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <lib/bit/bit.h>

#include <say.h>
#include <trivia/util.h>

const char *
fio_filename(int fd)
{
#ifdef TARGET_OS_LINUX
	char proc_path[32];
	static __thread char filename_path[PATH_MAX];

	sprintf(proc_path, "/proc/self/fd/%d", fd);

	ssize_t sz = readlink(proc_path, filename_path,
			      sizeof(filename_path));

	if (sz >= 0) {
		filename_path[sz] = '\0';
		return filename_path;
	}
#else /* TARGET_OS_LINUX */
	(void) fd;
#endif
	return ""; /* Not implemented. */
}

ssize_t
fio_read(int fd, void *buf, size_t count)
{
	ssize_t to_read = (size_t) count;
	while (to_read > 0) {
		ssize_t nrd = read(fd, buf, to_read);
		if (nrd < 0) {
			if (errno == EINTR) {
				errno = 0;
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return (ssize_t)count != to_read ? (ssize_t)count - to_read : -1;
			say_syserror("read, [%s]", fio_filename(fd));
			return -1; /* XXX: file position is unspecified */
		}
		if (nrd == 0)
			break;

		buf += nrd;
		to_read -= nrd;
	}
	return count - to_read;
}

ssize_t
fio_write(int fd, const void *buf, size_t count)
{
	ssize_t to_write = (ssize_t) count;
	while (to_write > 0) {
		ssize_t nwr = write(fd, buf, to_write);
		if (nwr < 0) {
			if (errno == EINTR) {
				errno = 0;
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return (ssize_t)count != to_write ? (ssize_t)count - to_write : -1;
			say_syserror("write, [%s]", fio_filename(fd));
			return -1; /* XXX: file position is unspecified */
		}
		if (nwr == 0)
			break;

		buf += nwr;
		to_write -= nwr;
	}
	return count - to_write;
}


ssize_t
fio_writev(int fd, struct iovec *iov, int iovcnt)
{
	assert(iov && iovcnt >= 0);
	ssize_t nwr;
restart:
	nwr = writev(fd, iov, iovcnt);
	if (nwr < 0) {
		if (errno == EINTR) {
			errno = 0;
			goto restart;
		}
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			say_syserror("writev, [%s]", fio_filename(fd));
	}
	return nwr;
}

off_t
fio_lseek(int fd, off_t offset, int whence)
{
	off_t effective_offset = lseek(fd, offset, whence);

	if (effective_offset == -1) {
		say_syserror("lseek, [%s]: offset=%jd, whence=%d",
			     fio_filename(fd), (intmax_t) offset, whence);
	} else if (whence == SEEK_SET && effective_offset != offset) {
		say_error("lseek, [%s]: offset set to unexpected value: "
			  "requested %jd effective %jd",
			  fio_filename(fd),
			  (intmax_t)offset, (intmax_t)effective_offset);
	}
	return effective_offset;
}

int
fio_truncate(int fd, off_t offset)
{
	int rc = ftruncate(fd, offset);
	if (rc)
		say_syserror("fio_truncate, [%s]: offset=%jd",
			     fio_filename(fd), (intmax_t) offset);
	return rc;
}


struct fio_batch *
fio_batch_new(void)
{
	int max_iov = sysconf(_SC_IOV_MAX);

	struct fio_batch *batch = (struct fio_batch *)
		malloc(sizeof(struct fio_batch) +
		       sizeof(struct iovec) * max_iov);
	if (batch == NULL)
		return NULL;

	fio_batch_reset(batch);
	batch->max_iov = max_iov;
	return batch;
}

void
fio_batch_delete(struct fio_batch *batch)
{
	free(batch);
}

size_t
fio_batch_add(struct fio_batch *batch, int count)
{
	assert(batch->iovcnt + count <= batch->max_iov);

	size_t total_bytes = 0;
	struct iovec *iov = batch->iov + batch->iovcnt;
	struct iovec *end = iov + count;
	for (; iov != end; ++iov) {
		assert(iov->iov_base != NULL && iov->iov_len > 0);
		total_bytes += iov->iov_len;
	}
	batch->iovcnt += count;
	batch->bytes += total_bytes;
	return total_bytes;
}

/**
 * Rotate batch after partial write.
 */
static inline void
fio_batch_rotate(struct fio_batch *batch, size_t bytes_written)
{
	/*
	 * writev(2) usually fully write all data on local filesystems.
	 */
	if (likely(bytes_written == batch->bytes)) {
		/* Full write */
		fio_batch_reset(batch);
		return;
	}

	assert(bytes_written < batch->bytes); /* Partial write */
	batch->bytes -= bytes_written;

	struct iovec *iov = batch->iov;
	struct iovec *iovend = iov + batch->iovcnt;
	for (; iov < iovend; ++iov) {
		if (iov->iov_len > bytes_written) {
			iov->iov_base = (char *) iov->iov_base + bytes_written;
			iov->iov_len -= bytes_written;
			break;
		}
		bytes_written -= iov->iov_len;
	}
	assert(iov < iovend); /* Partial write  */
	memmove(batch->iov, iov, (iovend - iov) * sizeof(struct iovec));
	batch->iovcnt = iovend - iov;
}

ssize_t
fio_batch_write(struct fio_batch *batch, int fd)
{
	ssize_t bytes_written = fio_writev(fd, batch->iov, batch->iovcnt);
	if (unlikely(bytes_written <= 0))
		return -1; /* Error */

	fio_batch_rotate(batch, bytes_written);
	return bytes_written;
}
