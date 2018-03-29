#ifndef TARANTOOL_BLOOM_H_INCLUDED
#define TARANTOOL_BLOOM_H_INCLUDED
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

/* Classic bloom filter with several improvements
 * 1) Cache oblivious:
 *  Putze, F.; Sanders, P.; Singler, J. (2007),
 *  "Cache-, Hash- and Space-Efficient Bloom Filters"
 *  http://algo2.iti.kit.edu/singler/publications/cacheefficientbloomfilters-wea2007.pdf
 * 2) Fast hash function calculation:
 *  Kirsch, Adam; Mitzenmacher, Michael (2006)
 *  "Less Hashing, Same Performance: Building a Better Bloom Filter"
 *   https://www.eecs.harvard.edu/~michaelm/postscripts/tr-02-05.pdf
 * 3) Using only one hash value that is splitted into several independent parts
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include "bit/bit.h"
#include "small/quota.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	/* Expected cache line of target processor */
	BLOOM_CACHE_LINE = 64,
};

typedef uint32_t bloom_hash_t;

/**
 * Cache-line-size block of bloom filter
 */
struct bloom_block {
	unsigned char bits[BLOOM_CACHE_LINE];
};

/**
 * Bloom filter data structure
 */
struct bloom {
	/* Number of buckets (blocks) in the table */
	uint32_t table_size;
	/* Number of hash function per value */
	uint16_t hash_count;
	/* Bit field table */
	struct bloom_block *table;
};

/* {{{ API declaration */

/**
 * Allocate and initialize an instance of bloom filter
 *
 * @param bloom - structure to initialize
 * @param number_of_values - estimated number of values to be added
 * @param false_positive_rate - desired false positive rate
 * @param quota - quota for memory allocation
 * @return 0 - OK, -1 - memory error
 */
int
bloom_create(struct bloom *bloom, uint32_t number_of_values,
	     double false_positive_rate, struct quota *quota);

/**
 * Free resources of the bloom filter
 *
 * @param bloom - the bloom filter
 * @param quota - quota for memory deallocation
 */
void
bloom_destroy(struct bloom *bloom, struct quota *quota);

/**
 * Add a value into the data set
 * @param bloom - the bloom filter
 * @param hash - hash of the value
 */
static void
bloom_add(struct bloom *bloom, bloom_hash_t hash);

/**
 * Query for presence of a value in the data set
 * @param bloom - the bloom filter
 * @param hash - hash of the value
 * @return true - the value could be in data set; false - the value is
 *  definitively not in data set
 *
 */
static bool
bloom_maybe_has(const struct bloom *bloom, bloom_hash_t hash);

/**
 * Return the expected false positive rate of a bloom filter.
 * @param bloom - the bloom filter
 * @param number_of_values - number of values stored in the filter
 * @return - expected false positive rate
 */
double
bloom_fpr(const struct bloom *bloom, uint32_t number_of_values);

/**
 * Calculate size of a buffer that is needed for storing bloom table
 * @param bloom - the bloom filter to store
 * @return - Exact size
 */
size_t
bloom_store_size(const struct bloom *bloom);

/**
 * Store bloom filter table to the given buffer
 * Other struct bloom members must be stored manually.
 * @param bloom - the bloom filter to store
 * @param table - buffer to store to
 * #return - end of written buffer
 */
char *
bloom_store(const struct bloom *bloom, char *table);

/**
 * Allocate table and load it from given buffer.
 * Other struct bloom members must be loaded manually.
 *
 * @param bloom - structure to load to
 * @param table - data to load
 * @param quota - quota for memory allocation
 * @return 0 - OK, -1 - memory error
 */
int
bloom_load_table(struct bloom *bloom, const char *table, struct quota *quota);

/* }}} API declaration */

/* {{{ API definition */

static inline void
bloom_add(struct bloom *bloom, bloom_hash_t hash)
{
	/* Using lower part of the has for finding a block */
	bloom_hash_t pos = hash % bloom->table_size;
	hash = hash / bloom->table_size;
	/* __builtin_prefetch(bloom->table + pos, 1); */
	const bloom_hash_t bloom_block_bits = BLOOM_CACHE_LINE * CHAR_BIT;
	/* bit_no in block is less than bloom_block_bits (512).
	 * split the given hash into independent lower part and high part. */
	bloom_hash_t hash2 = hash / bloom_block_bits + 1;
	for (bloom_hash_t i = 0; i < bloom->hash_count; i++) {
		bloom_hash_t bit_no = hash % bloom_block_bits;
		bit_set(bloom->table[pos].bits, bit_no);
		/* Combine two hashes to create required number of hashes */
		/* Add i**2 for better distribution */
		hash += hash2 + i * i;
	}
}

static inline bool
bloom_maybe_has(const struct bloom *bloom, bloom_hash_t hash)
{
	/* Using lower part of the has for finding a block */
	bloom_hash_t pos = hash % bloom->table_size;
	hash = hash / bloom->table_size;
	/* __builtin_prefetch(bloom->table + pos, 0); */
	const bloom_hash_t bloom_block_bits = BLOOM_CACHE_LINE * CHAR_BIT;
	/* bit_no in block is less than bloom_block_bits (512).
	 * split the given hash into independent lower part and high part. */
	bloom_hash_t hash2 = hash / bloom_block_bits + 1;
	for (bloom_hash_t i = 0; i < bloom->hash_count; i++) {
		bloom_hash_t bit_no = hash % bloom_block_bits;
		if (!bit_test(bloom->table[pos].bits, bit_no))
			return false;
		/* Combine two hashes to create required number of hashes */
		/* Add i**2 for better distribution */
		hash += hash2 + i * i;
	}
	return true;
}

/* }}} API definition */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BLOOM_H_INCLUDED */
