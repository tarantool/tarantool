/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "node_name.h"

#include <ctype.h>

bool
node_name_is_valid_n(const char *name, size_t len)
{
	if (len == 0 || len > NODE_NAME_LEN_MAX || !isalpha(*name))
		return false;
	const char *end = name + len;
	while (name < end) {
		char c = *(name++);
		if (!isalnum(c) && c != '-')
			return false;
		if (tolower(c) != c)
			return false;
	}
	return true;
}
