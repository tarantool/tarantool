#ifndef TARANTOOL_BOX_TUPLE_DICTIONARY_H_INCLUDED
#define TARANTOOL_BOX_TUPLE_DICTIONARY_H_INCLUDED
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
#include "field_def.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mh_strnu32_t;
typedef uint32_t (*field_name_hash_f)(const char *str, uint32_t len);
extern field_name_hash_f field_name_hash;

/**
 * Shared tuple field names hash. It is referenced by tuple format
 * and space definition.
 */
struct tuple_dictionary {
	/** Field names hash. Key - name, value - field number. */
	struct mh_strnu32_t *hash;
	/**
	 * Array of names. All of them are stored in monolit
	 * memory area.
	 */
	char **names;
	/** Length of a names array. */
	uint32_t name_count;
	/** Reference counter. */
	int refs;
};

/**
 * Create a new tuple dictionary.
 * @param fields Array of space fields.
 * @param field_count Length of @a fields.
 *
 * @retval     NULL Memory error.
 * @retval not NULL Tuple dictionary with one ref.
 */
struct tuple_dictionary *
tuple_dictionary_new(const struct field_def *fields, uint32_t field_count);

/**
 * Compute a tuple dictionary hash with PMurHash32_Process and return
 * the size of data processed.
 */
uint32_t
tuple_dictionary_hash_process(const struct tuple_dictionary *dict,
			      uint32_t *ph, uint32_t *pcarry);

/** Compare two tuple dictionaries. */
int
tuple_dictionary_cmp(const struct tuple_dictionary *a,
		     const struct tuple_dictionary *b);

/**
 * Swap content of two dictionaries. Reference counters are not
 * swaped.
 */
void
tuple_dictionary_swap(struct tuple_dictionary *a, struct tuple_dictionary *b);

/**
 * Decrement reference counter. If a new reference counter value
 * is 0, then the dictionary is deleted.
 */
void
tuple_dictionary_unref(struct tuple_dictionary *dict);

/** Increment reference counter. */
void
tuple_dictionary_ref(struct tuple_dictionary *dict);

/**
 * Get field number by a name.
 * @param dict Tuple dictionary.
 * @param name Name to search.
 * @param name_len Length of @a name.
 * @param name_hash Hash of @a name.
 * @param[out] fieldno Field number, if it is found.
 *
 * @retval  0 Field is found.
 * @retval -1 No such field.
 */
int
tuple_fieldno_by_name(struct tuple_dictionary *dict, const char *name,
		      uint32_t name_len, uint32_t name_hash, uint32_t *fieldno);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*TARANTOOL_BOX_TUPLE_DICTIONARY_H_INCLUDED*/
