/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "iostream.h"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

#include "sio.h"

static const struct iostream_vtab plain_iostream_vtab;

void
plain_iostream_create(struct iostream *io, int fd)
{
	assert(fd >= 0);
	io->vtab = &plain_iostream_vtab;
	io->data = NULL;
	io->fd = fd;
}

void
iostream_close(struct iostream *io)
{
	int fd = io->fd;
	iostream_destroy(io);
	close(fd);
}

static void
plain_iostream_destroy(struct iostream *io)
{
	(void)io;
}

static ssize_t
plain_iostream_read(struct iostream *io, void *buf, size_t count)
{
	assert(io->fd >= 0);
	ssize_t ret = sio_read(io->fd, buf, count);
	if (ret >= 0)
		return ret;
	if (sio_wouldblock(errno))
		return IOSTREAM_WANT_READ;
	return IOSTREAM_ERROR;
}

static ssize_t
plain_iostream_write(struct iostream *io, const void *buf, size_t count)
{
	assert(io->fd >= 0);
	ssize_t ret = sio_write(io->fd, buf, count);
	if (ret >= 0)
		return ret;
	if (sio_wouldblock(errno))
		return IOSTREAM_WANT_WRITE;
	return IOSTREAM_ERROR;
}

static ssize_t
plain_iostream_writev(struct iostream *io, const struct iovec *iov, int iovcnt)
{
	assert(io->fd >= 0);
	ssize_t ret = sio_writev(io->fd, iov, iovcnt);
	if (ret >= 0)
		return ret;
	if (sio_wouldblock(errno))
		return IOSTREAM_WANT_WRITE;
	return IOSTREAM_ERROR;
}

static const struct iostream_vtab plain_iostream_vtab = {
	/* .destroy = */ plain_iostream_destroy,
	/* .read = */ plain_iostream_read,
	/* .write = */ plain_iostream_write,
	/* .writev = */ plain_iostream_writev,
};

int
iostream_ctx_create(struct iostream_ctx *ctx, enum iostream_mode mode,
		    const struct uri *uri)
{
	(void)uri;
	assert(mode == IOSTREAM_SERVER || mode == IOSTREAM_CLIENT);
	ctx->mode = mode;
	return 0;
}

void
iostream_ctx_destroy(struct iostream_ctx *ctx)
{
	iostream_ctx_clear(ctx);
}

int
iostream_create(struct iostream *io, int fd, struct iostream_ctx *ctx)
{
	assert(ctx->mode == IOSTREAM_SERVER || ctx->mode == IOSTREAM_CLIENT);
	(void)ctx;
	plain_iostream_create(io, fd);
	return 0;
}
