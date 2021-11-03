/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdbool.h>
#include <netdb.h> /* NI_MAXHOST, NI_MAXSERV */
#include <limits.h> /* _POSIX_PATH_MAX */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct uri {
	char *scheme;
	char *login;
	char *password;
	char *host;
	char *service;
	char *path;
	char *query;
	char *fragment;
	int host_hint;
};

#define URI_HOST_UNIX "unix/"
#define URI_MAXHOST NI_MAXHOST
#define URI_MAXSERVICE _POSIX_PATH_MAX /* _POSIX_PATH_MAX always > NI_MAXSERV */


/**
 * Creates new @a uri structure according to passed @a str.
 * If @a str parsing failed function return -1, and fill
 * @a uri structure with zeros, otherwise return 0 and save
 * URI components in appropriate fields of @a uri. @a uri
 * can be safely destroyed in case this function fails.
 * If @str == NULL function fill uri structure with zeros
 * and return 0. This function doesn't set diag.
 */
int
uri_create(struct uri *uri, const char *str);

/**
 * Destroy previosly created @a uri. Should be called
 * after each `uri_create` function call. Safe to call
 * if uri_create failed.
 */
void
uri_destroy(struct uri *uri);

int
uri_format(char *str, int len, const struct uri *uri, bool write_password);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
