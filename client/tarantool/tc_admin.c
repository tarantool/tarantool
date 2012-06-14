
/*
 * Copyright (C) 2012 Mail.RU
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include "client/tarantool/tc_admin.h"

int tc_admin_connect(struct tc_admin *a, const char *host, int port)
{
	a->host = host;
	a->port = port;
	a->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (a->fd < 0)
		return -1;
	int opt = 1;
	if (setsockopt(a->fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1)
		goto error;
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(a->port);
	struct hostent *he = gethostbyname(a->host);
	if (he)
		memcpy(&addr.sin_addr, (void*)(he->h_addr), he->h_length);
	else 
		goto error;
	if (connect(a->fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
		goto error;
	return 0;
error:
	close(a->fd);
	return -1;
}

int tc_admin_reconnect(struct tc_admin *a)
{
	tc_admin_close(a);
	return tc_admin_connect(a, a->host, a->port);
}

void tc_admin_close(struct tc_admin *a)
{
	if (a->fd > 0)
		close(a->fd);
	a->fd = 0;
}

static int
tc_admin_send(struct tc_admin *a, char *buf, size_t size) {
	ssize_t rc, off = 0;
	do {
		rc = send(a->fd, buf + off, size - off, 0);
		if (rc <= 0)
			return -1;
		off += rc;
	} while (off != size);
	return 0;
}

int tc_admin_query(struct tc_admin *a, char *q)
{
	if (tc_admin_send(a, q, strlen(q)) == -1)
		return -1;
	if (tc_admin_send(a, "\n", 1) == -1)
		return -1;
	return 0;
}

int tc_admin_reply(struct tc_admin *a, char **r, size_t *size)
{
	char *buf = NULL;
	size_t off = 0;
	while (1) {
		char rx[8096];
		ssize_t rxi = recv(a->fd, rx, sizeof(rx), 0);
		if (rxi <= 0)
			break;
		char *bufn = realloc(buf, off + rxi + 1);
		if (bufn == NULL)
			break;
		buf = bufn;
		memcpy(buf + off, rx, rxi);
		off += rxi;
		buf[off] = 0;
		if (off >= 10) {
			if (!memcmp(buf, "---\r\n", 5) &&
			    !memcmp(buf + off - 5, "...\r\n", 5)) {
				*r = buf;
				*size = off;
				return 0;
			}
		}
	}
	if (buf)
		free(buf);
	return -1;
}
