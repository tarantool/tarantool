#ifndef TARANTOOL_BOX_TUPLE_BLOOM_H_INCLUDED
#define TARANTOOL_BOX_TUPLE_BLOOM_H_INCLUDED
/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "salad/bloom.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tuple;
struct key_def;

/**
 * Tuple bloom filter.
 *
 * Consists of a set of bloom filters, one per each partial key.
 * When a key is checked to be hashed in the bloom, all its
 * partial keys are checked as well, which lowers the probability
 * of false positive results.
 */
struct tuple_bloom {
	/**
	 * If the following flag is set, this is a legacy
	 * bloom filter that stores hashes only for full keys
	 * (see tuple_bloom_decode_legacy).
	 */
	bool is_legacy;
	/** Number of key parts. */
	uint32_t part_count;
	/** Array of bloom filters, one per each partial key. */
	struct bloom parts[0];
};

/**
 * Array of tuple hashes.
 */
struct tuple_hash_array {
	/** Number of hashes stored in the array. */
	uint32_t count;
	/** Capacity of the array of hashes. */
	uint32_t capacity;
	/** Array of hashes. */
	uint32_t *values;
};

/**
 * Tuple bloom filter builder.
 *
 * Construction of a bloom filter proceeds as follows.
 * First all tuple hashes are stored in a builder object.
 * Once all hashes have been stored, a bloom filter of
 * the optimal size and all the hashes are added to it.
 */
struct tuple_bloom_builder {
	/** Number of key parts. */
	uint32_t part_count;
	/** Hash arrays, one per each partial key. */
	struct tuple_hash_array parts[0];
};

/**
 * Create a new tuple bloom filter builder.
 * @param part_count - number of key parts
 * @return bloom filter builder on success or NULL on OOM
 */
struct tuple_bloom_builder *
tuple_bloom_builder_new(uint32_t part_count);

/**
 * Destroy a tuple bloom filter builder.
 * @param builder - bloom filter builder to delete
 */
void
tuple_bloom_builder_delete(struct tuple_bloom_builder *builder);

/**
 * Add a tuple hash to a tuple bloom filter builder.
 * @param builder - bloom filter builder
 * @param tuple - tuple to add
 * @param key_def - key definition
 * @param hashed_parts - number of key parts that have already
 *  been added to the builder
 * @return 0 on success, -1 on OOM
 */
int
tuple_bloom_builder_add(struct tuple_bloom_builder *builder,
			const struct tuple *tuple,
			const struct key_def *key_def,
			uint32_t hashed_parts);

/**
 * Create a new tuple bloom filter.
 * @param builder - bloom filter builder
 * @param fpr - desired false positive rate
 * @return bloom filter on success or NULL on OOM
 */
struct tuple_bloom *
tuple_bloom_new(struct tuple_bloom_builder *builder, double fpr);

/**
 * Delete a tuple bloom filter.
 * @param bloom - bloom filter to delete
 */
void
tuple_bloom_delete(struct tuple_bloom *bloom);

/**
 * Check if a tuple was stored in a tuple bloom filter.
 * @param bloom - bloom filter
 * @param tuple - tuple to check
 * @param key_def - key definition
 * @return true if the tuple may have been stored in the bloom,
 *  false if the tuple is definitely not in the bloom
 */
bool
tuple_bloom_maybe_has(const struct tuple_bloom *bloom,
		      const struct tuple *tuple,
		      const struct key_def *key_def);

/**
 * Check if a tuple matching a key was stored in a tuple bloom filter.
 * @param bloom - bloom filter
 * @param key - key to check
 * @param part_count - number of parts in the key
 * @param key_def - key definition
 * @return true if there may be a tuple matching the key stored in
 *  the bloom, false if there is definitely no such tuple
 */
bool
tuple_bloom_maybe_has_key(const struct tuple_bloom *bloom,
			  const char *key, uint32_t part_count,
			  const struct key_def *key_def);

/**
 * Return the size of a tuple bloom filter when encoded.
 * @param bloom - bloom filter
 * @return size of the bloom filter, in bytes
 */
size_t
tuple_bloom_size(const struct tuple_bloom *bloom);

/**
 * Encode a tuple bloom filter in MsgPack.
 * @param bloom - bloom filter
 * @param buf - buffer where to store the bloom filter
 * @return pointer to the first byte following encoded data
 */
char *
tuple_bloom_encode(const struct tuple_bloom *bloom, char *buf);

/**
 * Decode a tuple bloom filter from MsgPack.
 * @param data - pointer to buffer storing encoded bloom filter;
 *  on success it is advanced by the number of decoded bytes
 * @return the decoded bloom on success or NULL on OOM
 */
struct tuple_bloom *
tuple_bloom_decode(const char **data);

/**
 * Decode a legacy bloom filter from MsgPack.
 * @param data - pointer to buffer storing encoded bloom filter;
 *  on success it is advanced by the number of decoded bytes
 * @return the decoded bloom on success or NULL on OOM
 *
 * We used to store only full key bloom filters. This function
 * decodes such a bloom filter from MsgPack and initializes a
 * tuple_bloom object accordingly.
 */
struct tuple_bloom *
tuple_bloom_decode_legacy(const char **data);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_TUPLE_BLOOM_H_INCLUDED */
