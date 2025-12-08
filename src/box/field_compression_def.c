/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2025, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "field_compression_def.h"

#include "core/tt_static.h"
#include "diag.h"
#include "error.h"
#include "msgpuck.h"
#include "opt_def.h"
#include "small/region.h"
#include "trivia/util.h"

#include <PMurHash.h>

#if defined(ENABLE_TUPLE_COMPRESSION)
# include "field_compression_def_impl.c"
#else /* !defined(ENABLE_TUPLE_COMPRESSION) */

static const struct opt_def field_compression_def_reg[] = {OPT_END};

int
field_compression_def_cmp(const struct field_compression_def *def1,
			  const struct field_compression_def *def2)
{
	return (int)def1->type - (int)def2->type;
}

uint32_t
field_compression_def_hash_process(const struct field_compression_def *def,
				   uint32_t *ph, uint32_t *pcarry)
{
	PMurHash32_Process(ph, pcarry, &def->type, sizeof(def->type));
	return sizeof(def->type);
}

int
field_compression_def_check(struct field_compression_def *def)
{
	/* The switch makes the compiler warn if a new value is not handled. */
	switch (def->type) {
	case compression_type_MAX:
		diag_set(IllegalParams, "unknown compression type");
		return -1;
	case COMPRESSION_TYPE_NONE:
		break;
	}
	return 0;
}

#endif

int
field_compression_def_decode(const char **data,
			     struct field_compression_def *def,
			     struct region *region)
{
	/* Check if no compression had been specified previously. */
	if (def->type != COMPRESSION_TYPE_NONE) {
		diag_set(IllegalParams, "compression set twice");
		return -1;
	}

	/* Parse the string version of the "compression" field. */
	if (mp_typeof(**data) == MP_STR) {
		uint32_t str_len;
		const char *str = mp_decode_str(data, &str_len);
		def->type = strnindex(compression_type_strs, str,
				      str_len, compression_type_MAX);
		return 0;
	}

	/* Parse the array version: {"lz4"}. */
	if (mp_typeof(**data) == MP_ARRAY) {
		if (mp_decode_array(data) != 1) {
			diag_set(IllegalParams, "invalid compression value");
			return -1;
		}
		if (mp_typeof(**data) != MP_STR) {
			diag_set(IllegalParams,
				 "expected a string as the compression type");
			return -1;
		}
		uint32_t str_len;
		const char *str = mp_decode_str(data, &str_len);
		def->type = strnindex(compression_type_strs, str,
				      str_len, compression_type_MAX);
		return 0;
	}

	/* Parse the extended version of the field. */
	if (mp_typeof(**data) != MP_MAP) {
		diag_set(IllegalParams,
			 "compression field is expected to be a MAP or STR");
		return -1;
	}
	uint32_t map_size = mp_decode_map(data);
	if (map_size == 0) {
		diag_set(IllegalParams,
			 "compression name expected, got an empty table");
		return 0;
	}
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(**data) == MP_UINT) {
			if (def->type != COMPRESSION_TYPE_NONE) {
				diag_set(IllegalParams,
					 "compression type set twice");
				return -1;
			}

			/* compression[1] is the compression type. */
			uint32_t name_key = mp_decode_uint(data);
			if (name_key != 1) {
				diag_set(IllegalParams,
					 "unexpected compression key");
				return -1;
			}
			if (mp_typeof(**data) != MP_STR) {
				diag_set(IllegalParams,
					 "non-string compression type");
				return -1;
			}
			uint32_t str_len;
			const char *str = mp_decode_str(data, &str_len);
			def->type = strnindex(compression_type_strs, str,
					      str_len, compression_type_MAX);
			continue;
		}

		/* Handle compression options. */
		if (mp_typeof(**data) != MP_STR) {
			diag_set(IllegalParams,
				 "compression option name must be a string");
			return -1;
		}
		uint32_t opt_key_len;
		const char *opt_key = mp_decode_str(data, &opt_key_len);
		if (opts_parse_key(def, field_compression_def_reg, opt_key,
				   opt_key_len, data, region, false)) {
			diag_set(IllegalParams, tt_sprintf(
					"invalid compression table: %s",
					diag_last_error(diag_get())->errmsg));
			return -1;
		}
	}
	return 0;
}
