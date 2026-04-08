/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "tt_compression.h"
#include "trivia/config.h"
#include "diag.h"
#include "msgpuck.h"

#if defined(ENABLE_TUPLE_COMPRESSION)
# error unimplemented
#endif

const char *compression_type_strs[] = {
        "none",
};

int
compression_opts_decode(const char **data, struct compression_opts *opts,
			struct region *region)
{
	assert(opts->type == COMPRESSION_TYPE_NONE);

	/* Only used in EE option parser. */
	(void)region;
	(void)opts;

	/* Parse the string version of the "compression" field. */
	if (mp_typeof(**data) == MP_STR)
		goto check_compression_type;

	/* Parse the map version: {type = "none"}. */
	if (mp_typeof(**data) != MP_MAP) {
		diag_set(IllegalParams,
			 "compression is expected to be a MAP or STR");
		return -1;
	}
	uint32_t map_size = mp_decode_map(data);
	if (map_size == 0)
		return 0;
	if (map_size != 1) {
		diag_set(IllegalParams, "CE supports no compression options");
		return -1;
	}
	if (mp_typeof(**data) != MP_STR) {
		diag_set(IllegalParams,
			 "compression option name must be a string");
		return -1;
	}
	uint32_t str_len;
	const char *str = mp_decode_str(data, &str_len);
	if (str_len != strlen("type") || strncmp(str, "type", str_len) != 0) {
		diag_set(IllegalParams,
			 "CE only supports 'type' compression table key");
		return -1;
	}
	if (mp_typeof(**data) != MP_STR) {
		diag_set(IllegalParams, "non-string compression type");
		return -1;
	}

check_compression_type:
	str = mp_decode_str(data, &str_len);
	if (str_len != strlen("none") ||
	    strncmp(str, "none", str_len) != 0) {
		diag_set(IllegalParams, "unknown compression type");
		return -1;
	}
	return 0;
}
