/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "iostream.h"

#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include "sio.h"

static const struct iostream_vtab plain_iostream_vtab;

void
iostream_create(struct iostream *io, int fd)
{
	assert(fd >= 0);
	io->vtab = &plain_iostream_vtab;
	io->ctx = NULL;
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
plain_iostream_delete_ctx(void *ctx)
{
	(void)ctx;
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
	/* .delete_ctx = */ plain_iostream_delete_ctx,
	/* .read = */ plain_iostream_read,
	/* .write = */ plain_iostream_write,
	/* .writev = */ plain_iostream_writev,
};
