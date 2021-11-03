/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct uri_raw {
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

int
uri_raw_parse(struct uri_raw *uri, const char *str);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
