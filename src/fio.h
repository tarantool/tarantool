#ifndef TARANTOOL_FIO_H_INCLUDED
#define TARANTOOL_FIO_H_INCLUDED
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
/**
 * POSIX file I/O: take into account EINTR (read and write exactly
 * the requested number of bytes), log errors nicely, provide batch
 * writes.
 */
#include <stddef.h>
#include <sys/types.h>
#include <stdbool.h>
#include <sys/uio.h>
#include <assert.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

const char *
fio_filename(int fd);

struct iovec;
/**
 * Read up to N bytes from file into the buffer,
 * re-trying for interrupted reads. In case of a non-transient
 * error, writes a message to the error log.
 *
 * @param fd		file descriptor.
 * @param buf		pointer to the buffer.
 * @param count		how many bytes to read.
 *
 * @return When count is 0, returns 0. When count > SSIZE_MAX,
 *         the result is unspecified. Otherwise, returns the total
 *         number of bytes read, or -1 if error.  In case EOF is
 *         reached and less than count bytes are read, the actual
 *         number of bytes read is returned (can be 0 or more).
 *
 *         Blocking I/O:
 *         -------------
 *         If an error occurs after a few bytes were read, -1 is
 *         returned and current read offset is unspecified.
 *
 *         Non-blocking I/O
 *         ----------------
 *         Same as with blocking I/O but:
 *         If EAGAIN/EWOULDBLOCK occurs right away, returns -1,
 *         like read().
 *         If EAGAIN/EWOULDBLOCK occurs after a few bytes were
 *        read, the actual number of bytes read is returned.
 */
ssize_t
fio_read(int fd, void *buf, size_t count);

/**
 * Write the given buffer, re-trying for partial writes
 * (when interrupted by a signal, for instance). In case
 * of a non-transient error, writes a message to the error
 * log.
 *
 * @param fd		file descriptor.
 * @param buf		pointer to a buffer.
 * @param count		buffer size.
 *
 * @return When count is 0, returns 0. When count is positive,
 *         returns the total number of bytes written, or -1 if error.
 *
 *         Blocking I/O:
 *         -------------
 *         If an error occurs after a few bytes were written, -1 is
 *         returned and current write offset is unspecified.
 *         In other words, with blocking I/O this function
 *         returns either -1 or count.
 *
 *         Non-blocking I/O
 *         ----------------
 *         If EAGAIN/EWOULDBLOCK occurs and no data's been written
 *         yet, sets errno to EAGAIN and returns -1 (like read()).
 *         If EAGAIN/EWOULDBLOCK happens after a few bytes were
 *         written, the actual number of bytes written is
 *         returned.
 */
ssize_t
fio_write(int fd, const void *buf, size_t count);

/**
 * A simple wrapper around writev().
 * Re-tries write in case of EINTR.
 * In case of a serious error, writes a message to the error log.
 *
 * This function does not retry for partial writes because:
 * - it requires tedious byte counting, even when there is no
 *   partial write, just to find out what happened
 * - on most file systems, a partial write happens
 *   only in case of ENOSPC, which won't go away
 *   if we retry.
 * - there is a remote chance of partial write of a large iov,
 *   (> 4MB) due to a signal interrupt, but this is so rare that
 *   it's not worth slowing down the main case for the sake of it.
 * - to finish a partial write one has to allocate a copy of iov
 *
 * @param fd		file descriptor.
 * @param iov           a vector of buffer descriptors (@sa man
 *                      writev).
 * @param count		vector size
 *
 * @return When count is 0, returns 0. When count is positive,
 *         returns the total number of bytes written, or -1 if error.
 */
ssize_t
fio_writev(int fd, struct iovec *iov, int iovcnt);

/**
 * An error-reporting aware wrapper around lseek().
 *
 * @return	file offset value or -1 if error
 */
off_t
fio_lseek(int fd, off_t offset, int whence);

/** Truncate a file and log a message in case of error. */
int
fio_truncate(int fd, off_t offset);

/**
 * A helper wrapper around writev() to do batched
 * writes.
 */
struct fio_batch
{
	/** Total number of bytes in batched rows. */
	size_t bytes;
	/** Total number of batched rows.*/
	int iovcnt;
	/** A cap on how many rows can be batched. Can be set to INT_MAX. */
	int max_iov;
	/* Batched rows. */
	struct iovec iov[];
};

struct fio_batch *
fio_batch_new(void);

void
fio_batch_delete(struct fio_batch *batch);

static inline void
fio_batch_reset(struct fio_batch *batch)
{
	batch->bytes = 0;
	batch->iovcnt = 0;
}

static inline size_t
fio_batch_size(struct fio_batch *batch)
{
	return batch->bytes;
}

static inline int
fio_batch_unused(struct fio_batch *batch)
{
	return batch->max_iov - batch->iovcnt;
}

/**
 * Add a row to a batch.
 * @pre iovcnt is the number of iov elements previously 
 *      booked with fio_batch_book() and filled with data
 */
size_t
fio_batch_add(struct fio_batch *batch, int count);

/**
 * Ensure the iov has at least 'count' elements.
 */
static inline struct iovec *
fio_batch_book(struct fio_batch *batch, int count)
{
	if (batch->iovcnt + count <= batch->max_iov)
		return batch->iov + batch->iovcnt;
	return NULL;
}

/**
 * Write batch to fd using writev(2) and rotate batch.
 * In case of partial write batch will contain remaining data.
 * \sa fio_writev()
 */
ssize_t
fio_batch_write(struct fio_batch *batch, int fd);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_FIO_H_INCLUDED */

