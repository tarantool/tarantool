/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "iostream.h"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "diag.h"
#include "sio.h"
#include "ssl.h"
#include "uri/uri.h"

static const struct iostream_vtab plain_iostream_vtab;

void
plain_iostream_create(struct iostream *io, int fd)
{
	assert(fd >= 0);
	iostream_clear(io);
	io->vtab = &plain_iostream_vtab;
	io->fd = fd;
}

void
iostream_close(struct iostream *io)
{
	int fd = io->fd;
	iostream_destroy(io);
	/*
	 * Explicitly shut down the socket before closing its fd so that
	 * the connection will be terminated even if the Tarantool process
	 * forked and the child process did not close parent fds.
	 */
	shutdown(fd, SHUT_RDWR);
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
	assert(mode == IOSTREAM_SERVER || mode == IOSTREAM_CLIENT);
	ctx->mode = mode;
	ctx->ssl = NULL;
	const char *transport = uri_param(uri, "transport", 0);
	if (transport != NULL) {
		if (strcmp(transport, "ssl") == 0) {
			ctx->ssl = ssl_iostream_ctx_new(mode, uri);
			if (ctx->ssl == NULL)
				goto err;
		} else if (strcmp(transport, "plain") != 0) {
			diag_set(IllegalParams, "Invalid transport: %s",
				 transport);
			goto err;
		}
	}
	return 0;
err:
	iostream_ctx_clear(ctx);
	return -1;
}

void
iostream_ctx_destroy(struct iostream_ctx *ctx)
{
	if (ctx->ssl != NULL)
		ssl_iostream_ctx_delete(ctx->ssl);
	iostream_ctx_clear(ctx);
}

int
iostream_create(struct iostream *io, int fd, const struct iostream_ctx *ctx)
{
	assert(ctx->mode == IOSTREAM_SERVER || ctx->mode == IOSTREAM_CLIENT);
	if (ctx->ssl != NULL) {
		if (ssl_iostream_create(io, fd, ctx->mode, ctx->ssl) != 0)
			goto err;
	} else {
		plain_iostream_create(io, fd);
	}
	return 0;
err:
	iostream_clear(io);
	return -1;
}
