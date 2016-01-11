#ifndef TARANTOOL_URI_H_INCLUDED
#define TARANTOOL_URI_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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

#include <stddef.h>
#include <netdb.h> /* NI_MAXHOST, NI_MAXSERV */
#include <limits.h> /* _POSIX_PATH_MAX */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct uri {
	const char *scheme;
	size_t scheme_len;
	const char *login;
	size_t login_len;
	const char *password;
	size_t password_len;
	const char *host;
	size_t host_len;
	const char *service;
	size_t service_len;
	const char *path;
	size_t path_len;
	const char *query;
	size_t query_len;
	const char *fragment;
	size_t fragment_len;
	int host_hint;
};

#define URI_HOST_UNIX "unix/"
#define URI_MAXHOST NI_MAXHOST
#define URI_MAXSERVICE _POSIX_PATH_MAX /* _POSIX_PATH_MAX always > NI_MAXSERV */

int
uri_parse(struct uri *uri, const char *str);

char *
uri_format(const struct uri *uri);

int
uri_is_unix_path(const struct uri *uri);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_URI_H_INCLUDED */
