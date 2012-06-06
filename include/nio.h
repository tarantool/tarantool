#ifndef TARANTOOL_NIO_H_INCLUDED
#define TARANTOOL_NIO_H_INCLUDED
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
#include <stdbool.h>

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
nread(int fd, void *buf, size_t count);

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
nwrite(int fd, const void *buf, size_t count);

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
nwritev(int fd, struct iovec *iov, int iovcnt);

/**
 * An error-reporting aware wrapper around lseek().
 *
 * @return	file offset value or -1 if error
 */
off_t
nlseek(int fd, off_t offset, int whence);

/**
 * A helper wrapper around writev() to do batched
 * writes.
 */
struct nbatch
{
	/** Total number of bytes in batched rows. */
	ssize_t bytes;
	/** Total number of batched rows.*/
	int rows;
	/** A cap on how many can be batched. Can be set to INT_MAX. */
	int max_rows;
	/** A system cap on how many rows can be batched. */
	long max_iov;
	/* Batched rows. */
	struct iovec iov[];
};

struct nbatch *
nbatch_alloc(long max_iov);

/** Begin a new batch write. Set a cap on the number of rows in the batch.  */
void
nbatch_start(struct nbatch *batch, long max_rows);

static inline bool
nbatch_is_full(struct nbatch *batch)
{
	return batch->rows >= batch->max_iov || batch->rows >= batch->max_rows;
}

/**
 * Add a row to a batch.
 * @pre nbatch_is_full() == false
 */
void
nbatch_add(struct nbatch *batch, void *row, ssize_t row_len);

/**
 * Write all rows stacked into the batch.
 * In case of error, seeks back to the end of
 * the last fully written row.
 *
 * @return   The number of rows written.
 */
int
nbatch_write(struct nbatch *batch, int fd);

#endif /* TARANTOOL_NIO_H_INCLUDED */

