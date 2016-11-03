#ifndef TARANTOOL_BOX_TUPLE_GEN_H_INCLUDED
#define TARANTOOL_BOX_TUPLE_GEN_H_INCLUDED
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
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tuple;
struct key_def;
struct tuple_format;

typedef int (*tuple_compare_with_key_t)(const struct tuple *tuple_a,
			      const char *key,
			      uint32_t part_count,
			      const struct key_def *key_def);

typedef int (*tuple_compare_t)(const struct tuple *tuple_a,
			   const struct tuple *tuple_b,
			   const struct key_def *key_def);

tuple_compare_t
tuple_compare_create(const struct key_def *key_def);

tuple_compare_with_key_t
tuple_compare_with_key_create(const struct key_def *key_def);

/**
 * @brief Compare keys using key definition.
 * @param key_a MessagePack encoded key.
 * @param part_count_a Number of parts in the key_a.
 * @param key_b MessagePack encoded key.
 * @param part_count_b Number of parts in the key_b.
 * @param key_def key definition
 *
 * @retval 0  if key_a == key_b
 * @retval <0 if key_a < key_b
 * @retval >0 if key_a > key_b
 */
int
tuple_compare_key_raw(const char *key_a, uint32_t part_count_a,
		      const char *key_b, uint32_t part_count_b,
		      const struct key_def *key_def);

/**
 * @brief Compare a tuple with a key field by field using key definition.
 * @param format Tuple format.
 * @param tuple MessagePack array with tuple fields.
 * @param field_map Pointer BEFORE which offsets are stored.
 * @param key MessagePack encoded key
 * @param part_count number of parts in \a key
 * @param key_def key definition
 *
 * @retval 0  if key_fields(tuple) == parts(key)
 * @retval <0 if key_fields(tuple) < parts(key)
 * @retval >0 if key_fields(tuple) > parts(key)
 */
int
tuple_compare_with_key_default_raw(const struct tuple_format *format,
				   const char *tuple, uint32_t *field_map,
				   const char *key, uint32_t part_count,
				   const struct key_def *key_def);

/**
 * @brief Compare two tuples using field by field using key definition.
 * @param format_a First tuple format.
 * @param tuple_a MessagePack array with first tuple fields.
 * @param field_map_a Pointer right after last offset.
 * @param format_b Second tuple format.
 * @param tuple_b MessagePack array with second tuple fields.
 * @param field_map_b Pointer BEFORE which offsets on second tuple key fields
 *                    are stored.
 * @param key_def Key definition
 * @retval 0  if key_fields(tuple_a) == key_fields(tuple_b)
 * @retval <0 if key_fields(tuple_a) < key_fields(tuple_b)
 * @retval >0 if key_fields(tuple_a) > key_fields(tuple_b)
 */
int
tuple_compare_default_raw(const struct tuple_format *format_a,
			  const char *tuple_a, uint32_t *field_map_a,
			  const struct tuple_format *format_b,
			  const char *tuple_b, uint32_t *field_map_b,
			  const struct key_def *key_def);

#if defined(__cplusplus)
} /* extern "C" */

/** @sa tuple_compare_default_raw */
int
tuple_compare_default(const struct tuple *tuple_a, const struct tuple *tuple_b,
		      const struct key_def *key_def);

/** @sa tuple_compare_with_key_default_raw */
int
tuple_compare_with_key_default(const struct tuple *tuple_a, const char *key,
			       uint32_t part_count,
			       const struct key_def *key_def);


int
tuple_compare_with_key(const struct tuple *tuple, const char *key,
		       uint32_t part_count, const struct key_def *key_def);

int
tuple_compare(const struct tuple *tuple_a, const struct tuple *tuple_b,
	      const struct key_def *key_def);

#endif /* extern "C" */

#endif /* TARANTOOL_BOX_TUPLE_GEN_H_INCLUDED */
