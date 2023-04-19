/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "tarantool_ev.h"
#include "trivia/util.h"

/**
 * An IO stream object must not be used concurrently from different threads.
 * To catch this, we set the owner to the current thread before doing an IO
 * operation and clear it once done.
 */
#ifndef NDEBUG
# include "fiber.h"
# define IOSTREAM_OWNER_SET(io) ({					\
	assert((io)->owner == NULL);					\
	(io)->owner = cord();						\
})
# define IOSTREAM_OWNER_CLEAR(io) ({					\
	assert((io)->owner == cord());					\
	(io)->owner = NULL;						\
})
#else
# define IOSTREAM_OWNER_SET(io)		(void)(io)
# define IOSTREAM_OWNER_CLEAR(io)	(void)(io)
#endif

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct iostream;
struct iovec;
struct uri;

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

/** Possible values of iostream::flags. */
enum iostream_flag {
	/**
	 * Set if the iostream is encrypted (e.g. with SSL/TLS).
	 */
	IOSTREAM_IS_ENCRYPTED = 1 << 0,
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
	/** Bitwise combination of iostream_flag. */
	unsigned flags;
#ifndef NDEBUG
	/** Thread currently doing an IO operation on this IO stream. */
	struct cord *owner;
#endif
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
	io->flags = 0;
#ifndef NDEBUG
	io->owner = NULL;
#endif
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
	IOSTREAM_OWNER_SET(io);
	ssize_t ret = io->vtab->read(io, buf, count);
	IOSTREAM_OWNER_CLEAR(io);
	return ret;
}

/**
 * Writes up to count bytes from buf to a stream.
 * On success returns the number of bytes written (>= 0).
 * On failure returns iostream_status (< 0).
 */
static inline ssize_t
iostream_write(struct iostream *io, const void *buf, size_t count)
{
	IOSTREAM_OWNER_SET(io);
	ssize_t ret = io->vtab->write(io, buf, count);
	IOSTREAM_OWNER_CLEAR(io);
	return ret;
}

/**
 * Writes iovcnt buffers described by iov to a stream.
 * On success returns the number of bytes written.
 * On failure returns iostream_status (< 0).
 */
static inline ssize_t
iostream_writev(struct iostream *io, const struct iovec *iov, int iovcnt)
{
	IOSTREAM_OWNER_SET(io);
	ssize_t ret = io->vtab->writev(io, iov, iovcnt);
	IOSTREAM_OWNER_CLEAR(io);
	return ret;
}

enum iostream_mode {
	/** Uninitilized context (see iostream_ctx_clear). */
	IOSTREAM_MODE_UNINITIALIZED = 0,
	/** Server connection (accept). */
	IOSTREAM_SERVER,
	/** Client connection (connect). */
	IOSTREAM_CLIENT,
};

struct ssl_iostream_ctx;

/**
 * Context used for creating IO stream objects of a particular type.
 */
struct iostream_ctx {
	/** IO stream mode: server or client. */
	enum iostream_mode mode;
	/**
	 * Context used for creating encrypted streams. If it's NULL, then
	 * streams created with this context will be unencrypted.
	 */
	struct ssl_iostream_ctx *ssl;
};

/**
 * Clears an IO stream context struct. A cleared struct may be passed
 * to iostream_ctx_destroy (it'll be a no-op then), but passing it to
 * iostream_create is illegal.
 */
static inline void
iostream_ctx_clear(struct iostream_ctx *ctx)
{
	ctx->mode = IOSTREAM_MODE_UNINITIALIZED;
	ctx->ssl = NULL;
}

/**
 * Move constructor: copies src to dst and clears src.
 */
static inline void
iostream_ctx_move(struct iostream_ctx *dst, struct iostream_ctx *src)
{
	assert(src->mode == IOSTREAM_CLIENT || src->mode == IOSTREAM_SERVER);
	*dst = *src;
	iostream_ctx_clear(src);
}

/**
 * Creates an IO stream context for the given mode and URI.
 * On success returns 0. On failure returns -1, sets diag,
 * and clears the context struct (see iostream_ctx_clear).
 */
int
iostream_ctx_create(struct iostream_ctx *ctx, enum iostream_mode mode,
		    const struct uri *uri);

/**
 * Destroys an IO stream context and clears the context struct
 * (see iostream_ctx_clear).
 */
void
iostream_ctx_destroy(struct iostream_ctx *ctx);

/**
 * Creates an IO stream using the given context.
 * On success returns 0. On failure returns -1, sets diag,
 * and clears the iostream struct (see iostream_clear).
 */
int
iostream_create(struct iostream *io, int fd, const struct iostream_ctx *ctx);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
