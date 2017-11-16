/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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

#include "opt_def.h"
#include "msgpuck.h"
#include "bit/bit.h"
#include "small/region.h"
#include "error.h"
#include "diag.h"

const char *opt_type_strs[] = {
	/* [OPT_BOOL]	= */ "boolean",
	/* [OPT_UINT32]	= */ "unsigned",
	/* [OPT_INT64]	= */ "integer",
	/* [OPT_FLOAT]	= */ "float",
	/* [OPT_STR]	= */ "string",
	/* [OPT_STRPTR] = */ "string",
	/* [OPT_ENUM]   = */ "enum",
};

static int
opt_set(void *opts, const struct opt_def *def, const char **val,
	struct region *region)
{
	int64_t ival;
	uint64_t uval;
	double dval;
	uint32_t str_len;
	const char *str;
	char *ptr;
	char *opt = ((char *) opts) + def->offset;
	switch (def->type) {
	case OPT_BOOL:
		if (mp_typeof(**val) != MP_BOOL)
			return -1;
		store_bool(opt, mp_decode_bool(val));
		break;
	case OPT_UINT32:
		if (mp_typeof(**val) != MP_UINT)
			return -1;
		uval = mp_decode_uint(val);
		if (uval > UINT32_MAX)
			return -1;
		store_u32(opt, uval);
		break;
	case OPT_INT64:
		if (mp_read_int64(val, &ival) != 0)
			return -1;
		store_u64(opt, ival);
		break;
	case OPT_FLOAT:
		if (mp_read_double(val, &dval) != 0)
			return -1;
		store_double(opt, dval);
		break;
	case OPT_STR:
		if (mp_typeof(**val) != MP_STR)
			return -1;
		str = mp_decode_str(val, &str_len);
		str_len = MIN(str_len, def->len - 1);
		memcpy(opt, str, str_len);
		opt[str_len] = '\0';
		break;
	case OPT_STRPTR:
		if (mp_typeof(**val) != MP_STR)
			return -1;
		str = mp_decode_str(val, &str_len);
		if (str_len > 0) {
			ptr = (char *) region_alloc(region, str_len + 1);
			if (ptr == NULL) {
				diag_set(OutOfMemory, str_len + 1, "region",
					 "opt string");
				return -1;
			}
			memcpy(ptr, str, str_len);
			ptr[str_len] = '\0';
			assert (strlen(ptr) == str_len);
		} else {
			ptr = NULL;
		}
		*(const char **)opt = ptr;
		break;
	case OPT_ENUM:
		if (mp_typeof(**val) != MP_STR)
			return -1;
		str = mp_decode_str(val, &str_len);
		if (def->to_enum == NULL) {
			ival = strnindex(def->enum_strs, str, str_len,
					 def->enum_max);
		} else {
			ival = def->to_enum(str, str_len);
		}
		switch(def->enum_size) {
		case sizeof(uint8_t):
			store_u8(opt, (uint8_t)ival);
			break;
		case sizeof(uint16_t):
			store_u16(opt, (uint16_t)ival);
			break;
		case sizeof(uint32_t):
			store_u32(opt, (uint32_t)ival);
			break;
		case sizeof(uint64_t):
			store_u64(opt, (uint64_t)ival);
			break;
		default:
			unreachable();
		};
		break;
	default:
		unreachable();
	}
	return 0;
}

int
opts_parse_key(void *opts, const struct opt_def *reg, const char *key,
	       uint32_t key_len, const char **data, uint32_t errcode,
	       uint32_t field_no, struct region *region,
	       bool skip_unknown_options)
{
	char errmsg[DIAG_ERRMSG_MAX];
	bool found = false;
	for (const struct opt_def *def = reg; def->name != NULL; def++) {
		if (key_len != strlen(def->name) ||
		    memcmp(key, def->name, key_len) != 0)
			continue;

		if (opt_set(opts, def, data, region) != 0) {
			snprintf(errmsg, sizeof(errmsg), "'%.*s' must be %s",
				 key_len, key, opt_type_strs[def->type]);
			diag_set(ClientError, errcode, field_no, errmsg);
			return -1;
		}
		found = true;
		break;
	}
	if (!found) {
		if (skip_unknown_options) {
			mp_next(data);
		} else {
			snprintf(errmsg, sizeof(errmsg),
				 "unexpected option '%.*s'", key_len, key);
			diag_set(ClientError, errcode, field_no, errmsg);
			return -1;
		}
	}
	return 0;
}

/**
 * Populate key options from their msgpack-encoded representation
 * (msgpack map).
 */
int
opts_decode(void *opts, const struct opt_def *reg, const char **map,
	    uint32_t errcode, uint32_t field_no, struct region *region)
{
	assert(mp_typeof(**map) == MP_MAP);

	/*
	 * The implementation below has O(map_size * reg_size) complexity.
	 * DDL is not performance-critical, so this is not a problem.
	 */
	uint32_t map_size = mp_decode_map(map);
	for (uint32_t i = 0; i < map_size; i++) {
		if (mp_typeof(**map) != MP_STR) {
			diag_set(ClientError, errcode, field_no,
				 "key must be a string");
			return -1;
		}
		uint32_t key_len;
		const char *key = mp_decode_str(map, &key_len);
		if (opts_parse_key(opts, reg, key, key_len, map, errcode,
				   field_no, region, false) != 0)
			return -1;
	}
	return 0;
}
