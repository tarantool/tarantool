/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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
#include "systemd.h"

#if defined(WITH_NOTIFY_SOCKET)
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "say.h"

static int systemd_fd = -1;
static const char *sd_unix_path = NULL;

int systemd_init(void) {
	sd_unix_path = getenv("NOTIFY_SOCKET");
	if (sd_unix_path == NULL) {
		/* Do nothing if the path is not set. */
		return 0;
	}
	if ((sd_unix_path[0] != '@' && sd_unix_path[0] != '/') ||
	    (sd_unix_path[1] == '\0')) {
		say_error("systemd: NOTIFY_SOCKET contains bad value");
		goto error;
	}
	/* To be sure, that path to unix socket is OK */
	struct sockaddr_un sa = {
		.sun_family = AF_UNIX,
		.sun_path = { '\0' }
	};
	if (strlen(sd_unix_path) >= sizeof(sa.sun_path)) {
		say_error("systemd: NOTIFY_SOCKET is longer than MAX_UNIX_PATH");
		goto error;
	}
	if ((systemd_fd = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
		say_syserror("systemd: failed to create unix socket");
		goto error;
	}
	if (fcntl(systemd_fd, F_SETFD, FD_CLOEXEC) == -1) {
		say_syserror("systemd: fcntl failed to set FD_CLOEXEC");
		goto error;
	}

#if defined(HAVE_SO_NOSIGPIPE)
	int val = 1;
	if (setsockopt(systemd_fd, SOL_SOCKET, SO_NOSIGPIPE, (void*)&val,
			sizeof(val)) < 0) {
		say_syserror("systemd: failed to set NOSIGPIPE");
		goto error;
	}
#endif

	int sndbuf_sz = 4 * 1024 * 1024;
	if (setsockopt(systemd_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf_sz,
		      sizeof(int)) < 0) {
		say_syserror("systemd: failed to set sndbuf size");
		goto error;
	};
	return 0;
error:
	if (systemd_fd > 0) {
		close(systemd_fd);
		systemd_fd = -1;
	}
	sd_unix_path = NULL;
	return -1;
}

void systemd_free(void) {
	if (systemd_fd > 0)
		close(systemd_fd);
}

int systemd_notify(const char *message) {
	if (systemd_fd == -1 || sd_unix_path == NULL)
		return 0;

	struct sockaddr_un sa = {
		.sun_family = AF_UNIX,
	};

	strncpy(sa.sun_path, sd_unix_path, sizeof(sa.sun_path) - 1);
	if (sa.sun_path[0] == '@')
		sa.sun_path[0] = '\0';

	say_debug("systemd: sending message '%s'", message);
	int flags = 0;
#if defined(HAVE_MSG_NOSIGNAL)
	flags |= MSG_NOSIGNAL;
#endif
	ssize_t sent = sendto(systemd_fd, message, (size_t) strlen(message),
		flags, (struct sockaddr *) &sa, sizeof(sa));
	if (sent == -1) {
		say_syserror("systemd: failed to send message");
		return -1;
	}
	return sent;
}

int
systemd_vsnotify(const char *format, va_list ap)
{
	if (systemd_fd == -1 || sd_unix_path == NULL)
		return 0;
	char *buf = NULL;
	int rv = vasprintf(&buf, format, ap);
	if (rv < 0 || buf == NULL) {
		say_syserror("systemd: failed to format string '%s'", format);
		return -1;
	}
	rv = systemd_notify(buf);
	free(buf);
	return rv;
}

CFORMAT(printf, 1, 2) int
systemd_snotify(const char *format, ...)
{
	if (systemd_fd == -1 || sd_unix_path == NULL)
		return 0;
	va_list args;
	va_start(args, format);
	size_t res = systemd_vsnotify(format, args);
	va_end(args);
	return res;
}
#endif /* defined(WITH_NOTIFY_SOCKET) */
