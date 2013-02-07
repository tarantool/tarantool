#ifndef TARANTOOL_LIB_BITSET_INDEX_H_INCLUDED
#define TARANTOOL_LIB_BITSET_INDEX_H_INCLUDED
/*
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
 * @brief BitsetIndex - a bit index based on @link bitset @endlink.
 * @see bitset.h
 */

#include <lib/bitset/bitset.h>
#include <lib/bitset/iterator.h>

/**
 * @brief BitsetIndex
 */
struct bitset_index {
	/** @cond false **/
	struct bitset **bitsets;
	size_t capacity;
	void *(*realloc)(void *ptr, size_t size);
	/* A buffer used for rollback changes in bitset_insert */
	char *rollback_buf;
	/** @endcond **/
};

/**
 * @brief Construct \a index
 * @param index bitset index
 * @param realloc memory allocator to use
 * @retval 0 on success
 * @retval -1 on memory error
 */
int
bitset_index_create(struct bitset_index *index,
		    void *(*realloc)(void *ptr, size_t size));

/**
 * @brief Destruct \a index
 * @param index bitset index
 */
void
bitset_index_destroy(struct bitset_index *index);

/**
 * @brief Insert (\a key, \a value) pair into \a index.
 * Only one pair with same value can exist in the index.
 * If pair with same \a value is exist, it will be updated quietly.
 * The method is atomic, i.e. \a index will be in a consistent state after
 * a return even in case of error.
 * @param index object
 * @param key key
 * @param key_size size of the key
 * @param value value
 * @retval 0 on success
 * @retval -1 on memory error
 */
int
bitset_index_insert(struct bitset_index *index, const void *key, size_t key_size,
		    size_t value);

/**
 * @brief Remove a pair with \a value (*, \a value) from \a index.
 * @param index bitset index
 * @param value value
 */
void
bitset_index_remove_value(struct bitset_index *index, size_t value);

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
bitset_index_expr_all(struct bitset_expr *expr);

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
bitset_index_expr_equals(struct bitset_expr *expr, const void *key,
			 size_t key_size);

/**
 * @brief Initialize \a expr to iterate over a bitset index.
 * The \a expr can be then passed to @link bitset_index_init_iterator @endlink.
 *
 * 'All-Bits-Set' algorithm. Matches all pairs where all bits from \a key
 * is set in pair.key ((\a key & pair.key) == \a key).
 *
 * @param expr bitset expression
 * @retval 0 on success
 * @retval -1 on memory error
 * @see @link bitset_index_init_iterator @endlink
 * @see expr.h
 */
int
bitset_index_expr_all_set(struct bitset_expr *expr, const void *key,
			  size_t key_size);

/**
 * @brief Initialize \a expr to iterate over a bitset index.
 * The \a expr can be then passed to @link bitset_index_init_iterator @endlink.
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
bitset_index_expr_any_set(struct bitset_expr *expr, const void *key,
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
bitset_index_expr_all_not_set(struct bitset_expr *expr, const void *key,
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
bitset_index_init_iterator(struct bitset_index *index,
			   struct bitset_iterator *it,
			   struct bitset_expr *expr);

/**
 * @brief Checks if a (*, \a value) pair exists in \a index
 * @param index bitset index
 * @param value
 * @retval true if \a index contains pair with the \a value
 * @retval false otherwise
 */
bool
bitset_index_contains_value(struct bitset_index *index, size_t value);

/**
 * @brief Return a number of pairs in \a index.
 * @param index bitset index
 * @return number of pairs in \a index
 */
inline size_t
bitset_index_size(struct bitset_index *index)
{
	return bitset_cardinality(index->bitsets[0]);
}

#if defined(DEBUG)
void
bitset_index_dump(struct bitset_index *index, int verbose, FILE *stream);
#endif /* defined(DEBUG) */

#endif /* TARANTOOL_LIB_BITSET_INDEX_H_INCLUDED */
