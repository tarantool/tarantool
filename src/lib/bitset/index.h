#ifndef TARANTOOL_LIB_BITSET_INDEX_H_INCLUDED
#define TARANTOOL_LIB_BITSET_INDEX_H_INCLUDED
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
 * @brief bitset_index - a bit index based on @link bitset @endlink.
 *
 * @section Purpose
 *
 * bitset_index is an associative container that stores (key,
 * value) pairs in a way that is optimized for searching values
 * matching a logical expressions on bits of the key. The
 * organization structure of bitset_index makes it easy to respond
 * to queries like 'return all (key, value) pairs where the key
 * has bit i and bit j set'. The implementation supports
 * evaluation of arbitrary logical expressions represented in
 * Disjunctive Normal Form.
 *
 * To search over keys in a bitset_index, a logical expression
 * needs to be constructed.
 * logical expression. The expression can be constructed one time
 * and used for multiple queries. A search for an exact match for
 * a given key is not what bitset_index is designed for --
 * a conventional TREE or HASH index suits this task better.
 *
 * @section Organization
 *
 * bitset_index is a compressed bit matrix with dimensions N+1xK,
 * where N corresponds to the bit count of the longest key present
 * in the index, and K is the maximal value present in the index.
 * Each column in the matrix stands for a single bit of the key
 * and is represented by a single bitset.
 * If there is value k, which corresponding key has bit i set,
 * then bitset i+1 will have bit k set.
 * For example, if a pair with (key, value) is inserted to the
 * index and its key, has 0, 2, 5, 6 bits set then bitsets #1, #3,
 * #6, #7 are set at position = pair.value (@link bitset_test
 * bitset_test(bitset, pair.value) @endlink is true) and bitsets
 * #2, #4, #7 , ... are unset at the position.
 *
 * bitset_index also uses a special bitset #0 that is set to true
 * for each position where a pair with value = position exists in
 * an index. This bitset is mostly needed for evaluation
 * expressions with binary NOTs.
 *
 * A consequence of to the above design, is that in a bitset_index
 * one can have multiple pairs with same key, but all values in an
 * index must be unique.
 *
 * @section Performance
 *
 * For a certain kind of tasks bitset_index is more efficient both
 * speed- and memory- wise than a binary search tree or a hash
 * table.
 *
 * The complexity of @link bitset_insert @endlink operation is
 * mostly equivalent to inserting one value into \a k balanced
 * binary search trees, each of size \a m, where \a k is the number of
 * set bits in the key and \ m is the number of pairs in the index
 * divided by bitset page size.
 *
 * The complexity of iteration is linear from the number of pairs
 * in which the search expression evaluates to true. The
 * complexity of an iterator expression does not affect
 * iteration performance directly, which is more dependent
 * on the number of matching values.
 *
 * The actual performance heavily depends on the distribution of
 * values.  If the value space is dense, then internal bitsets are
 * also compact and better optimized for iteration.
 *
 * @section Limitations
 *
 * Key size is limited only by the available memory.
 * bitset_index automatically resizes on 'insert' if a key
 * contains more bits than in any key inserted thus far.
 *
 * Since values are used as a position in bitsets, the actual
 * range of values must be in [0..SIZE_MAX) range.
 *
 * @see bitset.h
 * @see expr.h
 * @see iterator.h
 */

#include "bitset/bitset.h"
#include "bitset/iterator.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * @brief BitsetIndex
 */
struct tt_bitset_index {
	/** @cond false **/
	/* Used bitsets */
	struct tt_bitset **bitsets;
	/* Capacity of bitsets array */
	size_t capacity;
	/* Memory allocator to use */
	void *(*realloc)(void *ptr, size_t size);
	/* A buffer used for rollback changes in bitset_insert */
	char *rollback_buf;
	/** @endcond **/
};

/**
 * @brief Construct \a index
 * @param index bitset index
 * @param realloc memory allocator to use
 */
void
tt_bitset_index_create(struct tt_bitset_index *index,
		       void *(*realloc)(void *ptr, size_t size));

/**
 * @brief Destruct \a index
 * @param index bitset index
 */
void
tt_bitset_index_destroy(struct tt_bitset_index *index);

/**
 * @brief Insert (\a key, \a value) pair into \a index.
 * \a value must be unique in the index.
 * This method is atomic, i.e. \a index will be in a consistent
 * state after a return even in case of error.
 *
 * @param index object
 * @param key key
 * @param key_size size of the key
 * @param value value
 * @retval 0 on success
 * @retval -1 on memory error
 */
int
tt_bitset_index_insert(struct tt_bitset_index *index, const void *key,
		       size_t key_size, size_t value);

/**
 * @brief Remove a pair with \a value (*, \a value) from \a index.
 * @param index bitset index
 * @param value value
 */
void
tt_bitset_index_remove_value(struct tt_bitset_index *index, size_t value);

/**
 * @brief Initialize \a expr to iterate over a bitset index.
 * The \a expr can be then passed to @link bitset_index_init_iterator @endlink.
 *
 * 'All' algorithm. Matches all pairs in a index.
 *
 * @param expr bitset expression
 * @retval 0 on success
 * @retval -1 on memory error
 * @see @link bitset_index_init_iterator @endlink
 * @see expr.h
 */
int
tt_bitset_index_expr_all(struct tt_bitset_expr *expr);

/**
 * @brief Initialize \a expr to iterate over a bitset index.
 * The \a expr can be then passed to @link bitset_index_init_iterator @endlink.
 *
 * 'Equals' algorithm. Matches all pairs where \a key exactly equals to
 * pair.key (\a key == pair.key).
 *
 * @param expr bitset expression
 * @param key key
 * @param key_size of \a key (in char, as sizeof returns)
 * @retval 0 on success
 * @retval -1 on memory error
 * @see @link bitset_index_init_iterator @endlink
 * @see expr.h
 */
int
tt_bitset_index_expr_equals(struct tt_bitset_expr *expr, const void *key,
			    size_t key_size);

/**
 * @brief Initialize \a expr to iterate over a bitset index.
 * The \a expr can be then passed to @link bitset_index_init_iterator @endlink.
 *
 * 'All-Bits-Set' algorithm. Matches all pairs where all bits from \a key
 * are set in pair.key ((\a key & pair.key) == \a key).
 *
 * @param expr bitset expression
 * @retval 0 on success
 * @retval -1 on memory error
 * @see @link bitset_index_init_iterator @endlink
 * @see expr.h
 */
int
tt_bitset_index_expr_all_set(struct tt_bitset_expr *expr, const void *key,
			     size_t key_size);

/**
 * @brief Initialize \a expr to iterate over a bitset index.
 * The \a expr can then be passed to @link bitset_index_init_iterator @endlink.
 *
 * 'Any-Bits-Set' algorithm. Matches all pairs where at least one bit from
 * \a key is set in pair.key ((\a key & pair.key) != 0).
 *
 * @param expr bitset expression
 * @retval 0 on success
 * @retval -1 on memory error
 * @see @link bitset_index_init_iterator @endlink
 * @see expr.h
 */
int
tt_bitset_index_expr_any_set(struct tt_bitset_expr *expr, const void *key,
			     size_t key_size);

/**
 * @brief Initialize \a expr to iterate over a bitset index.
 * The \a expr can be then passed to @link bitset_index_init_iterator @endlink.
 *
 * 'All-Bits-Not-Set' algorithm. Matches all pairs in the \a index, where all
 * bits from the \a key is not set in pair.key ((\a key & pair.key) == 0).
 *
 * @param expr bitset expression
 * @retval 0 on success
 * @retval -1 on memory error
 * @see @link bitset_index_init_iterator @endlink
 * @see expr.h
 */
int
tt_bitset_index_expr_all_not_set(struct tt_bitset_expr *expr, const void *key,
				 size_t key_size);

/**
 * @brief Initialize \a it using \a expr and bitsets used in \a index.
 *
 * @param index bitset index
 * @param it bitset iterator
 * @param expr bitset expression
 * @retval 0 on success
 * @retval 1 on memory error
 */
int
tt_bitset_index_init_iterator(struct tt_bitset_index *index,
			      struct tt_bitset_iterator *it,
			      struct tt_bitset_expr *expr);

/**
 * @brief Checks if a (*, \a value) pair exists in \a index
 * @param index bitset index
 * @param value
 * @retval true if \a index contains pair with the \a value
 * @retval false otherwise
 */
bool
tt_bitset_index_contains_value(struct tt_bitset_index *index, size_t value);

/**
 * @brief Return the number of pairs in \a index.
 * @param index bitset index
 * @return number of pairs in \a index
 */
inline size_t
tt_bitset_index_size(const struct tt_bitset_index *index)
{
	return tt_bitset_cardinality(index->bitsets[0]);
}

/**
 * @brief Returns the number of (key, value ) pairs where @a bit is set in key
 * @param index bitset index
 * @param bit bit
 * @retval the number of (key, value ) pairs where (@a bit & key) != 0
 */
inline size_t
tt_bitset_index_count(const struct tt_bitset_index *index, size_t bit)
{
	if (bit + 1 >= index->capacity)
		return 0;
	return tt_bitset_cardinality(index->bitsets[bit + 1]);
}

/**
 * @brief Return the number of bytes used by index. Only dynamically allocated
 * data are counted (i.e. sizeof(struct bitset_index) is not counted)
 * @param index bitset index
 * @return number of bytes used by index.
 */
size_t
tt_bitset_index_bsize(const struct tt_bitset_index *index);

#if defined(DEBUG)
void
tt_bitset_index_dump(struct tt_bitset_index *index, int verbose, FILE *stream);
#endif /* defined(DEBUG) */

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_BITSET_INDEX_H_INCLUDED */
