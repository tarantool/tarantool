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

struct uri_param;

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
	/** Count of URI parameters */
	int param_count;
	/** Different URI parameters */
	struct uri_param *params;
};

struct uri_set {
	/** Count of URIs */
	int uri_count;
	/** Array of URIs */
	struct uri *uris;
};

#define URI_HOST_UNIX "unix/"
#define URI_MAXHOST NI_MAXHOST
#define URI_MAXSERVICE _POSIX_PATH_MAX /* _POSIX_PATH_MAX always > NI_MAXSERV */

/**
 * Appends @a value to @a uri parameter with given @a name,
 * creating one if it doesn't exist.
 */
void
uri_add_param(struct uri *uri, const char *name, const char *value);

/**
 * Remove @a uri parameter and all its values.
 */
void
uri_remove_param(struct uri *uri, const char *name);

/**
 * Copy constructor for @a dst URI. Copies all fiels from
 * @a src URI to @dst URI.
 */
void
uri_copy(struct uri *dst, const struct uri *src);

/**
 * Move constructor for @a dst URI. Move all fields from
 * @a src URI to @a dst and clear @a src URI.
 */
void
uri_move(struct uri *dst, struct uri *src);

/**
 * Creates new @a uri structure according to passed @a str.
 * If @a str parsing failed function return -1, and fill
 * @a uri structure with zeros, otherwise return 0 and save
 * URI components in appropriate fields of @a uri. @a uri
 * can be safely destroyed in case this function fails.
 * If @str == NULL function fill uri structure with zeros
 * and return 0. Expected format of @a src string: "uri?query",
 * where query contains parameters separated by '&'. This
 * function doesn't set diag.
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

/**
 * Work same as `uri_create` function but could parse
 * string which contains several URIs separated by
 * commas. Create @a uri_set from appropriate @a str.
 * if @a str == NULL, create empty @a uri_set.
 */
int
uri_set_create(struct uri_set *uri_set, const char *str);

/**
 * Destroy previosly created @a uri_set. Should be called
 * after each `uri_set_create` function call.
 */
void
uri_set_destroy(struct uri_set *uri_set);

/**
 * Add single URI to @a uri_set. Don't forget to destroy @a uri after
 * calling this function.
 */
void
uri_set_add(struct uri_set *uri_set, struct uri *uri);

int
uri_format(char *str, int len, const struct uri *uri, bool write_password);

/**
 * Return @a uri parameter value by given @a idx. If parameter with @a name
 * does not exist or @a idx is greater than or equal to URI parameter value
 * count, return NULL.
 */
const char *
uri_param(const struct uri *uri, const char *name, int idx);

/**
 * Return count of values for @a uri parameter with given @a name.
 * If parameter with such @a name does not exist return 0.
 */
int
uri_param_count(const struct uri *uri, const char *name);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
