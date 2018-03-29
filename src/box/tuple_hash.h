#ifndef TARANTOOL_BOX_TUPLE_HASH_H_INCLUDED
#define TARANTOOL_BOX_TUPLE_HASH_H_INCLUDED
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
#include "key_def.h"
#include "tuple.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Initialize tuple_hash() and key_hash() function for the key_def
 * @param key_def key definition
 */
void
tuple_hash_func_set(struct key_def *def);

/**
 * Compute hash of a tuple field.
 * @param ph1 - pointer to running hash
 * @param pcarry - pointer to carry
 * @param field - pointer to field data
 * @param coll - collation to use for hashing strings or NULL
 * @return size of processed data
 *
 * This function updates @ph1 and @pcarry and advances @field
 * by the number of processed bytes.
 */
uint32_t
tuple_hash_field(uint32_t *ph1, uint32_t *pcarry, const char **field,
		 struct coll *coll);

/**
 * Compute hash of a key part.
 * @param ph1 - pointer to running hash
 * @param pcarry - pointer to carry
 * @param tuple - tuple to hash
 * @param part - key part
 * @return size of processed data
 *
 * This function updates @ph1 and @pcarry.
 */
uint32_t
tuple_hash_key_part(uint32_t *ph1, uint32_t *pcarry,
		    const struct tuple *tuple,
		    const struct key_part *part);

/**
 * Calculates a common hash value for a tuple
 * @param tuple - a tuple
 * @param key_def - key_def for field description
 * @return - hash value
 */
static inline uint32_t
tuple_hash(const struct tuple *tuple, const struct key_def *key_def)
{
	return key_def->tuple_hash(tuple, key_def);
}

/**
 * Calculate a common hash value for a key
 * @param key - full key (msgpack fields w/o array marker)
 * @param key_def - key_def for field description
 * @return - hash value
 */
static inline uint32_t
key_hash(const char *key, const struct key_def *key_def)
{
	return key_def->key_hash(key, key_def);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_TUPLE_HASH_H_INCLUDED */
