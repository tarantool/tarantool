/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/**
 * Name suitable for a node visible in the network. Its format matches the
 * sub-domain label in RFC 1035, section 2.3.1
 * (https://www.rfc-editor.org/rfc/rfc1035#section-2.3.1).
 *
 * It allows to use the node name as a sub-domain and a host name.
 *
 * The limitations are: max 63 symbols (not including term 0); only lowercase
 * letters, digits, and hyphen. Can start only with a letter. Note that the
 * sub-domain name rules say that uppercase is allowed but the names are
 * case-insensitive. In Tarantool the lowercase is enforced.
 */

enum {
	NODE_NAME_LEN_MAX = 63,
	NODE_NAME_SIZE_MAX = NODE_NAME_LEN_MAX + 1,
};

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** Check if the node name of the given length is valid. */
bool
node_name_is_valid_n(const char *name, size_t len);

static inline bool
node_name_is_valid(const char *name)
{
	return node_name_is_valid_n(name, strnlen(name, NODE_NAME_SIZE_MAX));
}

static inline const char *
node_name_str(const char *name)
{
	if (name == NULL || *name == 0)
		return "<no-name>";
	return name;
}

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */
