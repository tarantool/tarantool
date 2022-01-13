/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>

#include "tarantool_ev.h"
#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct iostream;
struct iovec;

/**
 * A negative status code is returned by an iostream read/write operation
 * in case it didn't succeed.
 */
enum iostream_status {
	/** IO error. Diag is set. */
	IOSTREAM_ERROR = -1,
	/**
	 * IOSTREAM_WANT_READ and IOSTREAM_WANT_WRITE are returned if
	 * the operation would block trying to read or write data from
	 * the fd. Diag is not set in this case. The caller is supposed
	 * to poll/select the fd if this status code is returned.
	 *
	 * Note, a read is allowed to return IOSTREAM_WANT_WRITE and
	 * a write is allowed to return IOSTREAM_WANT_READ, because
	 * the underlying protocol may do some sort of server-client
	 * negotiation under the hood. Use iostream_status_to_events
	 * to convert the status to libev events.
	 */
	IOSTREAM_WANT_READ = -2,
	IOSTREAM_WANT_WRITE = -3,
};

/**
 * Returns libev events corresponding to a status.
 */
static inline int
iostream_status_to_events(ssize_t status)
{
	assert(status < 0);
	switch (status) {
	case IOSTREAM_WANT_READ:
		return EV_READ;
	case IOSTREAM_WANT_WRITE:
		return EV_WRITE;
	default:
		unreachable();
		return 0;
	}
}

struct iostream_vtab {
	/** Destroys implementation-specific data. */
	void
	(*destroy)(struct iostream *io);
	/** See iostream_read. */
	ssize_t
	(*read)(struct iostream *io, void *buf, size_t count);
	/** See iostream_write. */
	ssize_t
	(*write)(struct iostream *io, const void *buf, size_t count);
	/** See iostream_writev. */
	ssize_t
	(*writev)(struct iostream *io, const struct iovec *iov, int iovcnt);
};

/**
 * An IO stream implements IO operations over a file descriptor.
 * Can be used to add some data processing transparently to the user.
 */
struct iostream {
	const struct iostream_vtab *vtab;
	/** Implementation specific data. */
	void *data;
	/** File descriptor used for IO. Set to -1 on destruction. */
	int fd;
};

/**
 * Clears a stream object. The stream fd is set to -1.
 */
static inline void
iostream_clear(struct iostream *io)
{
	io->vtab = NULL;
	io->data = NULL;
	io->fd = -1;
}

/**
 * Returns:
 *  - false after iostream_clear
 *  - true  after construction
 *  - false after iostream_destroy and iostream_close
 */
static inline bool
iostream_is_initialized(struct iostream *io)
{
	return io->fd >= 0;
}

/**
 * Move constructor: copies src to dst and clears src.
 */
static inline void
iostream_move(struct iostream *dst, struct iostream *src)
{
	assert(iostream_is_initialized(src));
	*dst = *src;
	iostream_clear(src);
}

/**
 * Creates a plain stream (reads/writes fd without any processing)
 * for the given file descriptor.
 */
void
plain_iostream_create(struct iostream *io, int fd);

/**
 * Destroys a stream and closes its fd. The stream fd is set to -1.
 */
void
iostream_close(struct iostream *io);

/**
 * Destroys a stream without closing fd. The stream fd is set to -1.
 */
static inline void
iostream_destroy(struct iostream *io)
{
	assert(io->fd >= 0);
	io->vtab->destroy(io);
	iostream_clear(io);
}

/**
 * Reads up to count bytes from a stream and stores them in buf.
 * On success returns the number of bytes read (>= 0); 0 means
 * that the other end closed the connection. On failure returns
 * iostream_status (< 0).
 */
static inline ssize_t
iostream_read(struct iostream *io, void *buf, size_t count)
{
	return io->vtab->read(io, buf, count);
}

/**
 * Writes up to count bytes from buf to a stream.
 * On success returns the number of bytes written (>= 0).
 * On failure returns iostream_status (< 0).
 */
static inline ssize_t
iostream_write(struct iostream *io, const void *buf, size_t count)
{
	return io->vtab->write(io, buf, count);
}

/**
 * Writes iovcnt buffers described by iov to a stream.
 * On success returns the number of bytes written.
 * On failure returns iostream_status (< 0).
 */
static inline ssize_t
iostream_writev(struct iostream *io, const struct iovec *iov, int iovcnt)
{
	return io->vtab->writev(io, iov, iovcnt);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
