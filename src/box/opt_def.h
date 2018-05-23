#ifndef TARANTOOL_BOX_OPT_DEF_H_INCLUDED
#define TARANTOOL_BOX_OPT_DEF_H_INCLUDED
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

#include "trivia/util.h"
#include <stddef.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum opt_type {
	OPT_BOOL,	/* bool */
	OPT_UINT32,	/* uint32_t */
	OPT_INT64,	/* int64_t */
	OPT_FLOAT,	/* double */
	OPT_STR,	/* char[] */
	OPT_STRPTR,	/* char*  */
	OPT_ENUM,	/* enum */
	OPT_ARRAY,	/* array */
	opt_type_MAX,
};

extern const char *opt_type_strs[];

/**
 * Decode enum stored in MsgPack.
 * @param str encoded data pointer (next to MsgPack ENUM header).
 * @param len str length.
 * @retval string index or hmax if the string is not found.
 */
typedef int64_t (*opt_def_to_enum_cb)(const char *str, uint32_t len);

/**
 * Decode MsgPack array callback.
 * All memory allocations returned by opt_def_to_array_cb with opt
 * [out] argument should be managed manually.
 * @param str encoded data pointer (next to MsgPack ARRAY header).
 * @param len array length (items count).
 * @param [out] opt pointer to store resulting value.
 * @param errcode Code of error to set if something is wrong.
 * @param field_no Field number of an option in a parent element.
 * @retval 0 on success.
 * @retval -1 on error.
 */
typedef int (*opt_def_to_array_cb)(const char **str, uint32_t len, char *opt,
				   uint32_t errcode, uint32_t field_no);

struct opt_def {
	const char *name;
	enum opt_type type;
	size_t offset;
	uint32_t len;

	const char *enum_name;
	int enum_size;
	const char **enum_strs;
	uint32_t enum_max;
	/** MsgPack data decode callbacks. */
	union {
		opt_def_to_enum_cb to_enum;
		opt_def_to_array_cb to_array;
	};
};

#define OPT_DEF(key, type, opts, field) \
	{ key, type, offsetof(opts, field), sizeof(((opts *)0)->field), \
	  NULL, 0, NULL, 0, {NULL} }

#define OPT_DEF_ENUM(key, enum_name, opts, field, to_enum) \
	{ key, OPT_ENUM, offsetof(opts, field), sizeof(int), #enum_name, \
	  sizeof(enum enum_name), enum_name##_strs, enum_name##_MAX, \
	  {(void *)to_enum} }

#define OPT_DEF_ARRAY(key, opts, field, to_array) \
	 { key, OPT_ARRAY, offsetof(opts, field), sizeof(((opts *)0)->field), \
	   NULL, 0, NULL, 0, {(void *)to_array} }

#define OPT_END {NULL, opt_type_MAX, 0, 0, NULL, 0, NULL, 0, {NULL}}

struct region;

/**
 * Populate key options from their msgpack-encoded representation
 * (msgpack map).
 */
int
opts_decode(void *opts, const struct opt_def *reg, const char **map,
	    uint32_t errcode, uint32_t field_no, struct region *region);

/**
 * Decode one option and store it into @a opts struct as a field.
 * @param opts[out] Options to decode to.
 * @param reg Options definition.
 * @param key Name of an option.
 * @param key_len Length of @a key.
 * @param data Option value.
 * @param errcode Code of error to set if something is wrong.
 * @param field_no Field number of an option in a parent element.
 * @param region Region to allocate OPT_STRPTR option.
 * @param skip_unknown_options If true, do not set error, if an
 *        option is unknown. Useful, when it is neccessary to
 *        allow to store custom fields in options.
 */
int
opts_parse_key(void *opts, const struct opt_def *reg, const char *key,
	       uint32_t key_len, const char **data, uint32_t errcode,
	       uint32_t field_no, struct region *region,
	       bool skip_unknown_options);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_OPT_DEF_H_INCLUDED */
