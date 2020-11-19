#ifndef TARANTOOL_LIB_BITSET_BITSET_H_INCLUDED
#define TARANTOOL_LIB_BITSET_BITSET_H_INCLUDED
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

/**
 * @file
 * @brief Module to work with arrays of bits (bitsets)
 *
 * Bitset is an array of bits where each bit can be set or unset
 * independently.  The bits of a @link bitset @endlink are indexed
 * by \a size_t position number.  Initially all bits are set to
 * false. You can use any values in range [0,SIZE_MAX).  The
 * container grows automatically.
 */

#include "bit/bit.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/types.h> /* ssize_t for rbtree */
#if defined(DEBUG)
#include <stdio.h> /* for dumping debug output to FILE */
#endif /* defined(DEBUG) */


/** @cond false */
#define RB_COMPACT 1
#include <small/rb.h>
/** @endcond */


#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/** @cond false */
struct tt_bitset_page {
	size_t first_pos;
	rb_node(struct tt_bitset_page) node;
	size_t cardinality;
	uint8_t data[];
};

typedef rb_tree(struct tt_bitset_page) tt_bitset_pages_t;
/** @endcond */

/**
 * Bitset
 */
struct tt_bitset {
	/** @cond false */
	tt_bitset_pages_t pages;
	size_t cardinality;
	void *(*realloc)(void *ptr, size_t size);
	/** @endcond */
};

/**
 * @brief Construct \a bitset
 * @param bitset bitset
 * @param realloc memory allocator to use
 */
void
tt_bitset_create(struct tt_bitset *bitset, void *(*realloc)(void *ptr,
							    size_t size));

/**
 * @brief Destruct \a bitset
 * @param bitset bitset
 */
void
tt_bitset_destroy(struct tt_bitset *bitset);

/**
 * @brief Test bit \a pos in \a bitset
 * @param bitset bitset
 * @param pos bit number
 * @retval true if \a pos is set in \a bitset
 * @retval false if \a pos is not set in \a bitset
 */
bool
tt_bitset_test(struct tt_bitset *bitset, size_t pos);

/**
 * @brief Set bit \a pos in \a bitset
 * @param bitset bitset
 * @param pos bit number
 * @retval 1 on success if previous value of \a pos was true
 * @retval 0 on success if previous value of \a pos was false
 * @retval -1 on memory error
 */
int
tt_bitset_set(struct tt_bitset *bitset, size_t pos);

/**
 * @brief Clear bit \a pos in \a bitset
 * @param bitset bitset
 * @param pos bit number
 * @retval 1 on success if previous value of \a pos was true
 * @retval 0 on success if previous value of \a pos was false
 * @retval -1 on memory error
 */
int
tt_bitset_clear(struct tt_bitset *bitset, size_t pos);

/**
 * @brief Return the number of bits set to \a true in \a bitset.
 * @param bitset bitset
 * @return returns the number of bits set to \a true in \a bitset.
 */
inline size_t
tt_bitset_cardinality(const struct tt_bitset *bitset) {
	return bitset->cardinality;
}

/**
 * @brief Bitset Information structure
 * @see bitset_info
 */
struct tt_bitset_info {
	/** Number of allocated pages */
	size_t pages;
	/** Data (payload) size of one page (in bytes) */
	size_t page_data_size;
	/** Full size of one page (in bytes, including padding and tree data) */
	size_t page_total_size;
	/** A multiplier by which an address of page data is aligned **/
	size_t page_data_alignment;
};

/**
 * @brief Fill information about \a bitset
 * @param bitset bitset
 * @param stat structure to fill
 */
void
tt_bitset_info(struct tt_bitset *bitset, struct tt_bitset_info *info);

#if defined(DEBUG)
void
tt_bitset_dump(struct tt_bitset *bitset, int verbose, FILE *stream);
#endif /* defined(DEBUG) */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_BITSET_BITSET_H_INCLUDED */
